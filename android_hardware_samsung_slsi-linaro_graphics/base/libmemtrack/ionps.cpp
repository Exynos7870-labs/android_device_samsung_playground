#include <iostream>
#include <errno.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <list>
#include <algorithm>
#include <iomanip>
#include <map>

#include <dirent.h>
#include <cerrno>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include <hardware/exynos/ion.h>

#define MAX_NAME_SIZE 64

using namespace std;

struct BufferNode;
struct ProcessNode;

struct mapNode {
    mapNode(BufferNode *_bufnode, ProcessNode *_procnode)
        : bufferNode(_bufnode), procNode(_procnode) {
            refcount = fd = -1;
        }
    BufferNode *bufferNode;
    ProcessNode *procNode;

    int fd;
    int refcount;
};

typedef map<int, ProcessNode> ProcessType;
typedef map<int, BufferNode> BufferType;
typedef map<int, mapNode> MapType;

struct BufferNode {
    BufferNode(unsigned int _id, unsigned int _flags, size_t _size,
               string _heaptype, string _heapname)
        : id(_id), flags(_flags), fileCount(-1), size(_size), HeapType(_heaptype), HeapName(_heapname) { }
    ~BufferNode() { };
    void setFileCount(int count)
    {
        fileCount = count;
    }
    void setAttachDevice(string device)
    {
        int offset = device.find_first_not_of(" \t\v\n");
        attachedDevice.push_back(device.substr(offset));
    }
    void setTraceNode(unsigned int refcount, ProcessNode *procNode);
    void setMapNode(unsigned int fd, ProcessNode *procNode);
    string getIonFlag(void);

    unsigned int id;
    unsigned int flags;
    int fileCount;
    size_t size;
    string HeapType;
    string HeapName;
    list<string> attachedDevice;
    MapType mapList;
};

struct ProcessNode {
    ProcessNode(unsigned int _pid)
        : pid(_pid) {
            setProcessName(_pid);
    }
    ~ProcessNode() { };

    void setProcessName(unsigned int pid);
    void setTraceNode(unsigned int refcount, BufferNode *bufferNode);
    void setMapNode(unsigned int fd, BufferNode *bufferNode);

    unsigned int pid;
    string comm;
    MapType mapList;
};

class MapTable {
public:
    MapTable()
        : totalIonMemory(0)
    {
    };
    ~MapTable() { };

    bool setupBuffer();
    bool setupTraceInfo();
    bool setupMapInfo();
    ProcessNode *setupProcessNode(unsigned int pid);

    BufferNode *getBufferNode(unsigned int buffer_id)
    {
        BufferType::iterator iter;

        iter = bufferList.find(buffer_id);
        if (iter != bufferList.end())
            return &iter->second;

        return nullptr;
    }

    ProcessNode *getProcessNode(unsigned int pid)
    {
        ProcessType::iterator iter;

        iter = processList.find(pid);
        if (iter != processList.end())
            return &iter->second;

        return nullptr;
    }

    void printBuffer(unsigned int buffer_id);
    void printBuffer(BufferNode &bufferNode);
    void printProcess(unsigned int pid);
    void printProcess(ProcessNode &processNode);
    void print();
    void printSummary(void);
private:
    unsigned int totalIonMemory;
    BufferType bufferList;
    ProcessType processList;
};

void BufferNode::setTraceNode(unsigned int refcount, ProcessNode *procNode)
{
    MapType::iterator iter;
    pair<MapType::iterator, bool> node;

    iter = mapList.find(procNode->pid);
    if (iter != mapList.end()) {
        iter->second.refcount = refcount;
        return;
    }

    node = mapList.emplace(procNode->pid, mapNode(this, procNode));
    node.first->second.refcount = refcount;
}

void BufferNode::setMapNode(unsigned int fd, ProcessNode *procNode)
{
    MapType::iterator iter;
    pair<MapType::iterator, bool> node;

    iter = mapList.find(procNode->pid);
    if (iter != mapList.end()) {
        iter->second.fd = fd;
        return;
    }

    node = mapList.emplace(procNode->pid, mapNode(this, procNode));
    node.first->second.fd = fd;
}

string BufferNode::getIonFlag(void)
{
    string tag;

    if (flags & ION_FLAG_CACHED)
        tag.append("C");
    if (flags & ION_FLAG_CACHED_NEEDS_SYNC)
        tag.append("N");
    if (flags & ION_FLAG_NOZEROED)
        tag.append("z");
    if (flags & ION_FLAG_PROTECTED)
        tag.append("P");
    if (flags & ION_FLAG_SYNC_FORCE)
        tag.append("S");
    if (flags & ION_FLAG_MAY_HWRENDER)
        tag.append("H");

    return tag;
}

void ProcessNode::setProcessName(unsigned int pid)
{
    if (pid == 0) {
        comm = "Kernel";
        return;
    }

    ostringstream ostr;
    ostr << "/proc/" << pid << "/comm";

    ifstream processDirectory(ostr.str());
    if (processDirectory && getline(processDirectory, comm))
        return;
    comm = "{{INVALID}}";
}

void ProcessNode::setTraceNode(unsigned int refcount, BufferNode *bufferNode)
{
    MapType::iterator iter;
    pair<MapType::iterator, bool> node;

    iter = mapList.find(bufferNode->id);
    if (iter != mapList.end()) {
        iter->second.refcount = refcount;
        return;
    }

    node = mapList.emplace(bufferNode->id, mapNode(bufferNode, this));
    node.first->second.refcount = refcount;
}

void ProcessNode::setMapNode(unsigned int fd, BufferNode *bufferNode)
{
    MapType::iterator iter;
    pair<MapType::iterator, bool> node;

    iter = mapList.find(bufferNode->id);
    if (iter != mapList.end()) {
        iter->second.fd = fd;
        return;
    }

    node = mapList.emplace(bufferNode->id, mapNode(bufferNode, this));
    node.first->second.fd = fd;
}

#define NUM_ION_BUFMAX 2048

const char ION_BUFFERS_PATH[] = "/sys/kernel/debug/ion/buffers";
const char ION_HEAPS_PATH[] = "/sys/kernel/debug/ion/heaps/";
const char ION_EVENT_PATH[] = "/sys/kernel/debug/ion/event";
static const char ION_DMABUF_PREFIX[] = "anon_inode:dmabuf_ion";
const char DMABUF_BUFINFO_PATH[] = "/sys/kernel/debug/dma_buf/bufinfo";
const char PROC_PATH[] = "/proc";
const char DMABUF_FOOTPRINT_PATH[] = "/sys/kernel/debug/dma_buf/footprint";

bool MapTable::setupBuffer()
{
    ifstream ion(ION_BUFFERS_PATH);
    if (!ion) {
        cout << "Buffer path does not exist (" << ION_BUFFERS_PATH << ")" << endl;
        return false;
    }

    // [  id]            heap heaptype flags size(kb)
    // [ 106] ion_system_heap   system  0x40    16912
    regex rexion_ion("\\[ *(\\d+)\\] +(\\w+)+ +(\\w+) +([x[:xdigit:]]+) +(\\d+).*");
    smatch mch;

    // 1-id, 2-heapname 3-heaptype, 3-flags, 4-size
    for (string line; getline(ion, line); ) {
        if (regex_match(line, mch, rexion_ion)) {
            unsigned int id = stoul(mch[1]);
            unsigned int flags = stoul(mch[4], 0, 16);
            size_t len = stoul(mch[5]);

            bufferList.emplace(id, BufferNode(id, flags, len * 1024, mch[3], mch[2]));
            totalIonMemory += len;
        }
    }

    ifstream dmabuf(DMABUF_BUFINFO_PATH);
    if (!dmabuf) {
        cout << "dmabuf path does not exist (" << DMABUF_BUFINFO_PATH << ")" << endl;
        return false;
    }

    //Dma-buf Objects:
    //size              flags           mode            count           exp_name
    //00004096          00000002        00000007        00000002        ion-333
    //  Attcached Devices:
    //  18500000.mali
    //Total 1 devices attached
    //
    regex rexion_dmabuf("(\\d+)\\s+(\\d{8})\\s+(\\d{8})\\s+(\\d{8})\\s+ion\\-(\\d+).*");
    for (string line; getline(dmabuf, line); ) {
        // 1-size 2-flags 3-mode 4-count 5-id
        if (regex_match(line, mch, rexion_dmabuf)) {
            unsigned int file_count = stoul(mch[4]);
            unsigned id = stoul(mch[5]);

            BufferNode *bufferNode = getBufferNode(id);
            if (!bufferNode)
                continue;

            bufferNode->setFileCount(file_count);

            // Attached Devices:
            getline(dmabuf, line);

            while (getline(dmabuf, line)) {
                // Total n devices attached
                if (!line.compare(0, 5, "Total"))
                    break;

                bufferNode->setAttachDevice(line);
            }
        }
    }

    return true;
}

bool MapTable::setupTraceInfo()
{
    //Dma-buf-trace Objects:
    //   exp_name          size        share       refcount
    //    ion-333        151552       151552              1
    DIR *proc = opendir(DMABUF_FOOTPRINT_PATH);

    if (!proc) {
        cout << "Footprint path does not exist (" << DMABUF_FOOTPRINT_PATH << ")" << endl;
        return false;
    }

    struct dirent *ProcessDirectory;

    while ((ProcessDirectory = readdir(proc)) != NULL) {
        char *strptr;
        unsigned int pid = strtoul(ProcessDirectory->d_name, &strptr, 10);
        if (strptr == ProcessDirectory->d_name)
            continue;

        /* file_directory : /d/dma_buf/footprint/<pid> */
        ostringstream file_directory;
        file_directory << DMABUF_FOOTPRINT_PATH << "/" << pid;

        ifstream trace(file_directory.str().c_str());
        if (!trace) {
            closedir(proc);
            cout << "Process file of footprint does not exist (" << file_directory.str() << ")" << endl;
            return false;
        }

        smatch mch;
        regex rexion_trace(" *ion\\-(\\d+) *(\\d+) *(\\d+) *(\\d+)");
        ProcessNode *procNode = nullptr;

        for (string line; getline(trace, line); ) {
            // 1-id 2-size 3-share 4-refcount
            if (regex_match(line, mch, rexion_trace)) {
                    unsigned int id = stoul(mch[1]);
                    unsigned int refcount = stoul(mch[4]);

                    BufferNode *bufferNode = getBufferNode(id);
                    if (!bufferNode)
                        continue;

                    if (!procNode)
                        procNode = setupProcessNode(pid);

                    bufferNode->setTraceNode(refcount, procNode);
                    procNode->setTraceNode(refcount, bufferNode);
            }
        }
    }

    closedir(proc);

    return true;
}

bool MapTable::setupMapInfo()
{
    DIR *proc = opendir(PROC_PATH);

    if (!proc) {
        cout << "Access Fail /proc" << endl;
        return false;
    }

    struct dirent *ProcessDirectory;

    /* ProcessDirectory : /proc/ */
    while ((ProcessDirectory = readdir(proc)) != NULL) {
        char *strptr;
        unsigned int pid = strtoul(ProcessDirectory->d_name, &strptr, 10);
        if ((strptr == ProcessDirectory->d_name) || (*strptr != '\0'))
            continue;

        /* fd_directory : /proc/<pid>/fd */
        ostringstream fd_directory;
        fd_directory << "/proc/" << pid << "/fd/";

        ProcessNode *procNode = nullptr;

        DIR *ProcessFileDirectory;
        if ((ProcessFileDirectory = opendir(fd_directory.str().c_str())) != NULL) {
            struct dirent *FileEntry;

            while ((FileEntry = readdir(ProcessFileDirectory)) != NULL) {
                unsigned int fd = strtoul(FileEntry->d_name, &strptr, 10);
                if (strptr == FileEntry->d_name)
                    continue;

                /* file_directory : /proc/<pid>/fd/<fd> */
                ostringstream file_directory;
                file_directory << fd_directory.str() << FileEntry->d_name;

                char symbolFile[MAX_NAME_SIZE] = {0, };
                if (readlink(file_directory.str().c_str(),
                             symbolFile, sizeof(symbolFile)) < 0)
                        continue;

                if (!strncmp(symbolFile, ION_DMABUF_PREFIX, sizeof(ION_DMABUF_PREFIX) - 1)) {
                    char *prefixStart = symbolFile + sizeof(ION_DMABUF_PREFIX);
                    unsigned int buffer_id = strtoul(prefixStart, &strptr, 10);
                    if (strptr == prefixStart)
                        continue;

                    BufferNode *bufferNode = getBufferNode(buffer_id);
                    if (!bufferNode) continue;

                    if (!procNode)
                        procNode = setupProcessNode(pid);

                    bufferNode->setMapNode(fd, procNode);
                    procNode->setMapNode(fd, bufferNode);
                }
            }
        }
        closedir(ProcessFileDirectory);
    }
    closedir(proc);

    return true;
}

ProcessNode *MapTable::setupProcessNode(unsigned int pid)
{
    ProcessNode *processNode;

    processNode = getProcessNode(pid);

    if (!processNode) {
        pair<ProcessType::iterator, bool> node;

        node = processList.emplace(pid, ProcessNode(pid));
        processNode = &node.first->second;
    }

    return processNode;
}

void MapTable::printBuffer(BufferNode &bufferNode)
{
//    size   flags   share            heap type   filecnt iommu_mapped…
// 2097152     0x8  299050 ion_system_heap system       2 12c00000@dpu
//
//                    PID            COMM        COUNT
//TRACKING PROCESSES:
//                    4078     com.xxx.yyy           1
//                    4012    cameraserver           1
// SHARING PROCESSES:
//                    4078     com.xxx.yyy         103
//                    3987  surfaceflinger          89
//                    4012    cameraserver          78
    cout << setw(5) << "id" << setw(10) << "size" << setw(8) << "flags";
    cout << setw(20) << "heap" << setw(10) << "heaptype" << setw(8) << "filecnt";
    cout << setw(3) << " attacheddevice" << endl;

    cout << setw(5) << bufferNode.id << setw(10) << bufferNode.size;
    cout << setw(8) << bufferNode.getIonFlag();
    cout << setw(20) << bufferNode.HeapName << setw(10) << bufferNode.HeapType;
    cout << setw(8) << bufferNode.fileCount << setw(3) << " ";

    for (auto attached : bufferNode.attachedDevice)
        cout << attached.c_str() << " ";

    cout << endl;

    cout << setw(20) << "SHARING PROCESS : " <<  setw(8) << "PID";
    cout << setw(20) << "COMM" << setw(12) << "FD" << endl;

    bool is_entry = false;
    for (auto node : bufferNode.mapList) {
        if (node.second.fd < 0)
            continue;
        cout << setw(28) << node.second.procNode->pid;
        cout << setw(20) << node.second.procNode->comm;
        cout << setw(12) << node.second.fd << endl;
        is_entry = true;
    }

    if (!is_entry)
        cout << setw(28) << "No entry" << endl;

    cout << setw(20) << "TRACKING PROCESS : " <<  setw(8) << "PID";
    cout << setw(20) << "COMM" << setw(12) << "TRACKCOUNT" << endl;

    is_entry = false;
    for (auto node : bufferNode.mapList) {
        if (node.second.refcount < 0)
            continue;
        cout << setw(28) << node.second.procNode->pid;
        cout << setw(20) << node.second.procNode->comm;
        cout << setw(12) << node.second.refcount << endl;
        is_entry = true;
    }

    if (!is_entry)
        cout << setw(28) << "No entry" << endl;

    cout << endl << endl;
}

void MapTable::printBuffer(unsigned int buffer_id)
{
    BufferNode *bufferNode = getBufferNode(buffer_id);
    if (!bufferNode) {
        cout << "Buffer does not exist for buffer id " << buffer_id << endl;
        return;
    }

    printBuffer(*bufferNode);
}

void MapTable::printProcess(ProcessNode &processNode)
{
    //Buffer list of ‘com.xxx.yyy’
    //    name     size            flags   share            heap trkcnt filecnt iommu_mapped…
    //  ion-47  1048576  cached|needsync  524288 ion_system_heap      1       2 16300000@g2d
    // ion-307  2097152         hwrender  699050 ion_system_heap      1       3 12c00000@dpu

    cout << "Buffer list of " << processNode.comm << " (" << processNode.pid << ")" << endl;

    cout << setw(5) << "id" << setw(5) << "fd" << setw(10) << "size" << setw(8) << "flags";
    cout << setw(20) << "heap" << setw(10) << "heaptype" << setw(8) << "trkcnt";
    cout << setw(8) << "filecnt" << setw(3) << " attacheddevice" << endl;

    for (auto node : processNode.mapList) {
        BufferNode *bufferNode = node.second.bufferNode;

        cout << setw(5) << bufferNode->id << setw(5) << node.second.fd;
        cout << setw(10) << bufferNode->size << setw(8) << bufferNode->getIonFlag();
        cout << setw(20) << bufferNode->HeapName << setw(10) << bufferNode->HeapType;
        cout << setw(8) << node.second.refcount;
        cout << setw(8) << bufferNode->fileCount << setw(3) << " ";

        for (auto attached : bufferNode->attachedDevice)
            cout << attached.c_str() << " ";

        cout << endl;
    }
    cout << endl << endl;
}

void MapTable::printProcess(unsigned int pid)
{
    ProcessNode *procNode = getProcessNode(pid);
    if (!procNode) {
        cout << "Process does not exist for process id " << pid << endl;
        return;
    }

    printProcess(*procNode);
}

void MapTable::print(void)
{
    for (auto buffer : bufferList)
        printBuffer(buffer.second);

    for (auto process : processList)
        printProcess(process.second);
}

void MapTable::printSummary(void)
{
    cout << endl;

    cout << "Total ion memory : " << totalIonMemory << "KB" << endl << endl;

    cout << setw(20) << "Ion memory by process:" << endl;
    cout << setw(10) << "PID" << setw(16) << "Resident Size" << setw(21) << "Proportional Size" << "    Process" << endl;

    // Total Pss by process:
    // PID        RSS         PSS    Process
    //   0     62472K      62472K    Kernel
    // 2526   104256K      78192K    composer@2.2-se
    for (auto process : processList) {
        ProcessNode procNode = process.second;
        unsigned int pss = 0;
        unsigned int rss = 0;

        for (auto node : procNode.mapList) {
            BufferNode *bufferNode = node.second.bufferNode;

            pss += bufferNode->size / bufferNode->mapList.size();
            rss += bufferNode->size;
        }

        cout << setw(10) << procNode.pid << setw(15) << rss / 1024 << "K" << setw(20) << pss / 1024;
        cout << "K    " << procNode.comm << endl;
    }
}

static void printEvent(void)
{
    string line;
    ifstream event(ION_EVENT_PATH);
    if (!event) {
        cout << "Event path does not exist (" << ION_EVENT_PATH << ")" << endl;
        return;
    }

    while (getline(event, line))
        cout << line << endl;
}

static vector<string> HeapList;

static void printHeap(string heapName)
{
    ifstream heap(ION_HEAPS_PATH + heapName);
    if (!heap) {
        cout << "Heap path does not exist (" << ION_HEAPS_PATH + heapName << ")" << endl;
        return;
    }

    string line;
    while (getline(heap, line))
        cout << line << endl;

    cout << endl;
}

static void printHelp(void)
{
    cout << "usage : ionps [-aesh] [-b BUFFER ID] [-p PROCESS ID] [-H HEAP NAME]" << endl;

    cout << setw(5) << "-b" << setw(10) << "--buffer" << setw(20) << "<buffer index>";
    cout << "   show process information that owns request buffer" << endl;
    cout << setw(5) << "-p" << setw(10) << "--process" << setw(20) << "<process id>";
    cout << "   show buffer information that requested process owns" << endl;
    cout << setw(5) << "-H" << setw(10) << "--heap" << setw(20) << "<heap name>";
    cout << "   show /d/ion/heaps/<heap_name>" << endl;
    cout << setw(5) << "-e" << setw(10) << "--event" << setw(20) << " ";
    cout << "   show /d/ion/event" <<endl;
    cout << setw(5) << "-a" << setw(10) << "--all" << setw(20) << " ";
    cout <<  "   show every buffer, process in detail" << endl;
    cout << setw(5) << "-s" << setw(10) << "--summary" << setw(20) << " ";
    cout <<  "   show summary such as ion total memory, and rss, pss by process" << endl;
    cout << setw(5) << "-h" << setw(10) << "--help" << setw(20) << " ";
    cout << "   This help message" << endl << endl;

    cout << "Available heap name: ";

    for (unsigned int i = 0; i < HeapList.size(); i++) {
        if (!(i & 3))
            cout << endl << setw(10) << " ";
        cout << HeapList[i] << ", ";
    }
    cout << "all (show every heap)" << endl;
}

static void setupHeapName(void)
{
    DIR *proc = opendir(ION_HEAPS_PATH);

    if (!proc)
        return;

    struct dirent *HeapDirectory;
    while ((HeapDirectory = readdir(proc)) != NULL) {
        string directory = HeapDirectory->d_name;

        if (!directory.compare(0, 1, "."))
            continue;

        if (!directory.compare(directory.size() - 6, 6, "shrink"))
            continue;

        HeapList.push_back(HeapDirectory->d_name);
    }

    closedir(proc);
}

static bool checkHeapName(string option)
{
    for (auto heap : HeapList) {
        if (heap == option)
            return true;
    }

    return false;
}

static int args_to_num(const char *optarg)
{
    int num;
    char *end;

    num = strtoul(optarg, &end, 10);
    if (optarg == end) {
        cout << "Unknown input number" << endl;

        return -1;
    }

    return num;
}

int main(int argc, char *argv[])
{
    setupHeapName();

    if (argc == 1) {
        printHelp();
        return 0;
    }

    MapTable maptable;

    if (!(maptable.setupBuffer() && maptable.setupTraceInfo() && maptable.setupMapInfo()))
        return -1;

    static const struct option long_options[] = {
        {"buffer",    required_argument,  0,          'b'},
        {"process",   required_argument,  0,          'p'},
        {"heap",      required_argument,  0,          'H'},
        {"event",     required_argument,  0,          'e'},
        {"all",       no_argument,        0,          'a'},
        {"summary",   no_argument,        0,          's'},
        {"help",      no_argument,        0,          'h'},
        {0, 0, 0, 0}
    };

    int c, option_index = 0;
    if ((c = getopt_long(argc, argv, "b:p:H:eash", long_options, &option_index)) == -1) {
        cout << "No argument" << endl;

        return -1;
    }

    switch (c) {
        case 'a':
            maptable.print();
            break;
        case 'b':
            maptable.printBuffer(args_to_num(optarg));
            break;
        case 'p':
            maptable.printProcess(args_to_num(optarg));
            break;
        case 'H':
	    if (!HeapList.size())
		    cout << "No support about option -H, --heap" << endl;

            if (!strncmp(optarg, "all", 3)) {
                for (unsigned int i = 0; i < HeapList.size(); i++)
                    printHeap(HeapList[i]);
            } else if (!checkHeapName(optarg)) {
                cout << "Unknown heap name " << optarg << endl;
                return -1;
            } else {
                printHeap(optarg);
            }
            break;
        case 'e':
            printEvent();
            break;
        case 'h':
            printHelp();
            break;
        case 's':
            maptable.printSummary();
            break;
        case '?':
            cout << "Unknown option " << optopt << " @ " << optind << endl;
            return -1;
        default:
            cout << "Error found during parsing command line options" << endl;
            return -1;
    }

    return 0;
}
