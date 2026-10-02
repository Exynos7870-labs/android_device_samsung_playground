#!/usr/bin/env python3
"""Host-test the actual zoneinfo parser in an Android 15 lmkd.cpp.

Run after applying the LMKD patch:
    python3 tests/test_lmkd_zoneinfo.py --source /path/to/system/memory/lmkd/lmkd.cpp

Only file I/O and Android logging are stubbed. Parser functions and types are
extracted from the supplied source, not reimplemented here. Fixtures model the
3.18 kernel's mm/vmstat.c output and the modern per-node format.
"""

import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

SOURCE = None
SANITIZE = False


def section(source, start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def zone(node=0, name="DMA", *, free=100, inactive=50, active=40,
         high=30, protection=(0, 7, 4), node_stats=None, modern=False):
    lines = [f"Node {node}, zone {name:>8}"]
    if node_stats is not None:
        lines += [" per-node stats", f"      nr_inactive_file {node_stats[0]}",
                  f"      nr_active_file {node_stats[1]}"]
    prefix = "nr_zone" if modern else "nr"
    lines += [f"  pages free     {free}", "        min      10",
              "        low      20", f"        high     {high}",
              "        scanned  0", "        spanned  2000",
              "        present  2000", "        managed  1900",
              f"    nr_free_pages {free}", f"    {prefix}_inactive_file {inactive}",
              f"    {prefix}_active_file {active}", "    nr_free_cma 5"]
    if modern:
        # Some backported kernels retain old aliases. Do not double-count them.
        lines += [f"    nr_inactive_file {inactive}", f"    nr_active_file {active}"]
    lines += ["        protection: (" + ", ".join(map(str, protection)) + ")",
              "  pagesets", "    cpu: 0", "              count: 1",
              "              high:  6", "              batch: 1",
              "  vm stats threshold: 4", "  all_unreclaimable: 0",
              "  start_pfn: 0", "  inactive_ratio: 1"]
    return "\n".join(lines) + "\n"


PREAMBLE = r"""
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#define ZONEINFO_PATH "/proc/zoneinfo"
#define NODE_STATS_MARKER " per-node stats"
#define STRINGIFY_INTERNAL(x) #x
#define STRINGIFY(x) STRINGIFY_INTERNAL(x)
#define ALOGE(...) ((void)0)
struct reread_data { const char* const filename; int fd; };
static std::vector<char> input;
static char* reread_file(struct reread_data*) { return input.data(); }
"""

MAIN = r"""
int main() {
    std::string text((std::istreambuf_iterator<char>(std::cin)),
                     std::istreambuf_iterator<char>());
    input.assign(text.begin(), text.end());
    input.push_back('\0');
    struct zoneinfo zi;
    int result = zoneinfo_parse(&zi);
    printf("{\"result\":%d", result);
    if (!result) {
        printf(",\"reserve\":%lld,\"inactive\":%lld,\"active\":%lld,\"nodes\":[",
               (long long)zi.totalreserve_pages, (long long)zi.total_inactive_file,
               (long long)zi.total_active_file);
        for (int n = 0; n < zi.node_count; ++n) {
            const auto& node = zi.nodes[n];
            printf("%s{\"id\":%d,\"inactive\":%lld,\"active\":%lld,\"zones\":[",
                   n ? "," : "", node.id, (long long)node.fields.field.nr_inactive_file,
                   (long long)node.fields.field.nr_active_file);
            for (int z = 0; z < node.zone_count; ++z) {
                const auto& fields = node.zones[z].fields.field;
                printf("%s{\"free\":%lld,\"min\":%lld,\"high\":%lld,\"present\":%lld}",
                       z ? "," : "", (long long)fields.nr_free_pages, (long long)fields.min,
                       (long long)fields.high, (long long)fields.present);
            }
            printf("]}");
        }
        printf("]");
    }
    puts("}");
}
"""


class ZoneinfoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if SOURCE is None:
            raise unittest.SkipTest("Pass --source pointing to the patched lmkd.cpp")
        source = SOURCE.read_text()
        types = section(source, "/* Fields to parse in /proc/zoneinfo */",
                        "/* Fields to parse in /proc/meminfo */")
        match_enum = section(source, "enum field_match_result {", "struct adjslot_list {")
        helpers = section(source, "static bool parse_int64(", "/*\n * Read file content")
        parser = section(source, "/*\n * /proc/zoneinfo parsing routines",
                         "/* /proc/meminfo parsing routines */")
        cls.temp = tempfile.TemporaryDirectory(prefix="lmkd-zoneinfo-test-")
        cls.addClassCleanup(cls.temp.cleanup)
        harness = Path(cls.temp.name) / "parser.cpp"
        cls.binary = Path(cls.temp.name) / "parser"
        harness.write_text(PREAMBLE + types + match_enum + helpers + parser + MAIN)
        command = shlex.split(os.environ.get("CXX", "g++"))
        command += ["-std=gnu++17", "-O1" if SANITIZE else "-O2", "-Wall", "-Wextra",
                    "-Werror", "-Wno-missing-field-initializers"]
        if SANITIZE:
            command += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
        try:
            subprocess.run(command + [str(harness), "-o", str(cls.binary)],
                           check=True, capture_output=True, text=True, timeout=60)
        except subprocess.CalledProcessError as error:
            raise RuntimeError(error.stderr) from error

    def parse(self, text):
        result = subprocess.run([str(self.binary)], input=text, capture_output=True,
                                text=True, check=True, timeout=5)
        return json.loads(result.stdout)

    def check_totals(self, result, *, reserve, inactive, active, zones):
        self.assertEqual(result["result"], 0)
        self.assertEqual(result["reserve"], reserve)
        self.assertEqual(result["inactive"], inactive)
        self.assertEqual(result["active"], active)
        self.assertEqual([len(node["zones"]) for node in result["nodes"]], zones)

    def test_legacy_single_zone(self):
        result = self.parse(zone(free=123, inactive=80, active=70))
        self.check_totals(result, reserve=37, inactive=80, active=70, zones=[1])
        self.assertEqual(result["nodes"][0]["zones"][0],
                         {"free": 123, "min": 10, "high": 30, "present": 2000})

    def test_legacy_multiple_zones(self):
        text = zone() + zone(name="Normal", free=200, inactive=100, active=80,
                             high=90, protection=(0, 11, 9))
        self.check_totals(self.parse(text), reserve=138, inactive=150, active=120, zones=[2])

    def test_legacy_multiple_nodes(self):
        text = zone(node=2, inactive=11, active=10) + zone(node=5, inactive=21, active=20)
        result = self.parse(text)
        self.check_totals(result, reserve=74, inactive=32, active=30, zones=[1, 1])
        self.assertEqual([node["id"] for node in result["nodes"]], [2, 5])

    def test_modern_does_not_double_count_zones(self):
        text = zone(node_stats=(500, 600), modern=True)
        text += zone(name="Normal", modern=True, inactive=10000, active=20000)
        self.check_totals(self.parse(text), reserve=74, inactive=500, active=600, zones=[2])

    def test_modern_multiple_nodes(self):
        text = zone(node=0, node_stats=(500, 600), modern=True)
        text += zone(node=1, node_stats=(100, 200), modern=True)
        self.check_totals(self.parse(text), reserve=74, inactive=600, active=800, zones=[1, 1])

    def test_modern_skips_empty_first_zone(self):
        text = "Node 0, zone DMA\n  pages free 0\n  present 0\n"
        text += zone(name="Normal", node_stats=(500, 600), modern=True)
        self.check_totals(self.parse(text), reserve=37, inactive=500, active=600, zones=[1])

    def test_empty_input(self):
        self.assertEqual(self.parse("")["result"], -1)

    def test_truncated_legacy_header(self):
        self.assertEqual(self.parse("Node 0, zone DMA\n")["result"], -1)

    def test_truncated_modern_new_node_header(self):
        text = zone(node_stats=(500, 600), modern=True) + "Node 1, zone DMA\n"
        self.assertEqual(self.parse(text)["result"], -1)

    def test_truncated_zone_body(self):
        self.assertEqual(self.parse(zone().split("  pagesets")[0])["result"], -1)

    def test_invalid_legacy_file_counter(self):
        text = zone().replace("nr_inactive_file 50", "nr_inactive_file invalid")
        self.assertEqual(self.parse(text)["result"], -1)

    def test_invalid_modern_node_counter(self):
        text = zone(node_stats=(500, 600), modern=True)
        self.assertEqual(self.parse(text.replace("nr_active_file 600", "nr_active_file invalid"))
                         ["result"], -1)

    def test_zone_array_limit(self):
        for modern in (False, True):
            with self.subTest(modern=modern):
                text = zone(node_stats=(500, 600) if modern else None, modern=modern)
                text += "".join(zone(name=f"Zone{i}", modern=modern) for i in range(1, 6))
                self.assertEqual(self.parse(text)["result"], 0)
                self.assertEqual(self.parse(text + zone(name="Overflow", modern=modern))
                                 ["result"], -1)

    def test_node_array_limit(self):
        for modern in (False, True):
            with self.subTest(modern=modern):
                text = "".join(zone(node=i, node_stats=(500, 600) if modern else None,
                                    modern=modern) for i in range(3))
                self.assertEqual(self.parse(text)["result"], -1)


if __name__ == "__main__":
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("--source", required=True, type=Path)
    arguments.add_argument("--sanitize", action="store_true", help="Enable UndefinedBehaviorSanitizer")
    options, remaining = arguments.parse_known_args()
    SOURCE, SANITIZE = options.source, options.sanitize
    unittest.main(argv=[__file__, *remaining])
