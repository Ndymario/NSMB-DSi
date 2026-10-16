// Headless test harness: boots a ROM in stock melonDS (DS or DSi mode, direct
// boot), feeds it a button script, saves screenshots, and decodes the DSi
// module's boot report from emulated RAM.
//
//   harness --console dsi --rom build/nsmb-dsi.nds --frames 900 \
//           --press 300:start --shot 900:out.ppm --report
//
// BIOS/firmware/NAND paths come from flags (see --help); tools/run.sh fills
// them in from your melonDS settings.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Args.h"
#include "DSi.h"
#include "DSi_NAND.h"
#include "GPU.h"
#include "NDS.h"
#include "NDSCart.h"
#include "Platform.h"
#include "SPI_Firmware.h"

#include "dsi/report.hpp"

using namespace melonDS;

namespace melonDS::Platform { extern int g_logLevel; extern unsigned g_saveWrites; }

namespace {

[[noreturn]] void die(const std::string& msg)
{
	fprintf(stderr, "harness: %s\n", msg.c_str());
	exit(2);
}

std::vector<u8> readFile(const std::string& path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		die("cannot read " + path);
	return {std::istreambuf_iterator<char>(f), {}};
}

template<size_t N>
std::unique_ptr<std::array<u8, N>> readBios(const std::string& path)
{
	auto data = readFile(path);
	if (data.size() != N)
		die(path + ": expected " + std::to_string(N) + " bytes");
	auto out = std::make_unique<std::array<u8, N>>();
	memcpy(out->data(), data.data(), N);
	return out;
}

// DS key bits as KEYINPUT/EXTKEYIN order melonDS uses (bit set = released).
const std::map<std::string, int> KEYS = {
	{"a", 0}, {"b", 1}, {"select", 2}, {"start", 3}, {"right", 4}, {"left", 5},
	{"up", 6}, {"down", 7}, {"r", 8}, {"l", 9}, {"x", 10}, {"y", 11},
};

struct Press { u32 frame, hold, mask; };
struct Shot { u32 frame; std::string path; };
struct Dump { u32 frame, address, length; bool arm7; };
struct Touch { u32 frame, hold; u16 x, y; };

struct Options {
	std::string console = "ds";
	std::string rom;
	u32 frames = 600;
	std::vector<Press> presses;
	std::vector<Shot> shots;
	std::vector<Dump> dumps;
	std::vector<Touch> touches;
	bool report = false;
	std::string bios9, bios7, firmware;
	std::string dsiBios9, dsiBios7, dsiFirmware, nand;
};

// "300:start" or "300+10:a+b" (frame, optional hold length, keys)
Press parsePress(const std::string& spec)
{
	auto colon = spec.find(':');
	if (colon == std::string::npos)
		die("bad --press " + spec);
	std::string when = spec.substr(0, colon), keys = spec.substr(colon + 1);
	Press p{0, 6, 0};
	auto plus = when.find('+');
	p.frame = std::stoul(when.substr(0, plus));
	if (plus != std::string::npos)
		p.hold = std::stoul(when.substr(plus + 1));
	std::stringstream ss(keys);
	for (std::string k; std::getline(ss, k, '+');) {
		auto it = KEYS.find(k);
		if (it == KEYS.end())
			die("unknown key " + k);
		p.mask |= 1u << it->second;
	}
	return p;
}

Options parse(int argc, char** argv)
{
	Options o;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto next = [&]() -> std::string { if (++i >= argc) die("missing value for " + a); return argv[i]; };
		if (a == "--console") o.console = next();
		else if (a == "--rom") o.rom = next();
		else if (a == "--frames") o.frames = std::stoul(next());
		else if (a == "--press") o.presses.push_back(parsePress(next()));
		else if (a == "--shot") { auto s = next(); auto c = s.find(':'); o.shots.push_back({(u32)std::stoul(s.substr(0, c)), s.substr(c + 1)}); }
		else if (a == "--dump" || a == "--dump7") {
			// F:ADDR:LEN, hex address and length; --dump7 reads through the ARM7 bus
			unsigned f, addr, len;
			if (sscanf(next().c_str(), "%u:%x:%x", &f, &addr, &len) != 3)
				die("bad " + a + ", expected FRAME:ADDR:LEN");
			o.dumps.push_back({f, addr, len, a == "--dump7"});
		}
		else if (a == "--touch") {
			// F[+HOLD]:X,Y on the bottom screen
			unsigned f, hold = 6, x, y;
			auto spec = next();
			if (sscanf(spec.c_str(), "%u+%u:%u,%u", &f, &hold, &x, &y) != 4 && sscanf(spec.c_str(), "%u:%u,%u", &f, &x, &y) != 3)
				die("bad --touch, expected FRAME[+HOLD]:X,Y");
			o.touches.push_back({f, hold, (u16)x, (u16)y});
		}
		else if (a == "--report") o.report = true;
		else if (a == "--log") { auto l = next(); Platform::g_logLevel = l == "debug" ? 0 : l == "info" ? 1 : l == "warn" ? 2 : 3; }
		else if (a == "--bios9") o.bios9 = next();
		else if (a == "--bios7") o.bios7 = next();
		else if (a == "--firmware") o.firmware = next();
		else if (a == "--dsi-bios9") o.dsiBios9 = next();
		else if (a == "--dsi-bios7") o.dsiBios7 = next();
		else if (a == "--dsi-firmware") o.dsiFirmware = next();
		else if (a == "--nand") o.nand = next();
		else if (a == "--help" || a == "-h") {
			puts("harness --rom ROM [--console ds|dsi] [--frames N] [--press F[+HOLD]:keys]... [--shot F:out.ppm]... [--report]\n"
			     "        [--bios9 F --bios7 F --firmware F] [--dsi-bios9 F --dsi-bios7 F --dsi-firmware F --nand F] [--log debug|info|warn|error]");
			exit(0);
		}
		else die("unknown argument " + a);
	}
	if (o.rom.empty())
		die("--rom is required");
	if (o.console != "ds" && o.console != "dsi")
		die("--console must be ds or dsi");
	return o;
}

std::unique_ptr<NDS> makeConsole(const Options& o)
{
	NDSArgs args;
	args.JIT = std::nullopt;
	if (!o.bios9.empty()) args.ARM9BIOS = readBios<ARM9BIOSSize>(o.bios9);
	if (!o.bios7.empty()) args.ARM7BIOS = readBios<ARM7BIOSSize>(o.bios7);

	if (o.console == "ds") {
		if (!o.firmware.empty()) {
			auto fw = readFile(o.firmware);
			args.Firmware = Firmware(fw.data(), fw.size());
		}
		return std::make_unique<NDS>(std::move(args));
	}

	if (o.dsiBios9.empty() || o.dsiBios7.empty() || o.dsiFirmware.empty() || o.nand.empty())
		die("DSi mode needs --dsi-bios9, --dsi-bios7, --dsi-firmware and --nand");
	auto fw = readFile(o.dsiFirmware);
	args.Firmware = Firmware(fw.data(), fw.size());

	auto bios7i = readBios<DSiBIOSSize>(o.dsiBios7);
	auto nandFile = Platform::OpenFile(o.nand, Platform::FileMode::ReadWriteExisting);
	if (!nandFile)
		die("cannot open NAND " + o.nand);
	DSi_NAND::NANDImage nand(nandFile, &(*bios7i)[0x8308]);
	if (!nand)
		die("cannot parse NAND " + o.nand);

	DSiArgs dsiArgs{
		std::move(args),
		readBios<DSiBIOSSize>(o.dsiBios9),
		std::move(bios7i),
		std::move(nand),
		std::nullopt,
		false, // FullBIOSBoot
		true,  // DSP HLE
	};
	return std::make_unique<DSi>(std::move(dsiArgs));
}

void writePPM(NDS& nds, const std::string& path)
{
	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
		die("cannot write " + path);
	fprintf(f, "P6\n256 384\n255\n");
	int fb = nds.GPU.FrontBuffer;
	for (int screen = 0; screen < 2; screen++) {
		const u32* px = nds.GPU.Framebuffer[fb][screen].get();
		for (int i = 0; i < 256 * 192; i++) {
			u32 c = px ? px[i] : 0;
			u8 rgb[3] = {u8(c >> 16), u8(c >> 8), u8(c)};
			fwrite(rgb, 1, 3, f);
		}
	}
	fclose(f);
}

void readMem(NDS& nds, u32 addr, void* out, size_t size)
{
	for (size_t i = 0; i < size; i += 4) {
		u32 w = nds.ARM9Read32(addr + i);
		memcpy(static_cast<u8*>(out) + i, &w, std::min<size_t>(4, size - i));
	}
}

std::string hex(u32 v) { char b[16]; snprintf(b, sizeof b, "0x%08X", v); return b; }
std::string ascii(const u32* words, int n)
{
	std::string s;
	for (int i = 0; i < n * 4; i++) {
		char c = reinterpret_cast<const char*>(words)[i];
		s += (c >= 32 && c < 127) ? c : '.';
	}
	return s;
}

void printReport(NDS& nds)
{
	dsi::SharedBlock blk{};
	readMem(nds, DSI_SHARED_BLOCK, &blk, sizeof blk);
	printf("{\n  \"arm7\": {\"magic_ok\": %s, \"flags\": %u, \"sndexcnt\": \"%s\", \"scfg_rom7\": \"%s\", \"scfg_ext7\": \"%s\", \"mmem_checker7\": %u},\n",
	       blk.magic == dsi::SHARED_MAGIC7 ? "true" : "false", blk.flags, hex(blk.sndexcnt).c_str(),
	       hex(blk.scfgRom7).c_str(), hex(blk.scfgExt7).c_str(), blk.mmemChecker7);

	dsi::BootReport r{};
	if (blk.arm9Report)
		readMem(nds, blk.arm9Report, &r, sizeof r);
	if (r.magic != dsi::REPORT_MAGIC || r.version != dsi::REPORT_VERSION) {
		printf("  \"arm9\": null\n}\n");
		return;
	}
	printf("  \"arm9\": {\n");
	printf("    \"address\": \"%s\", \"console\": %s, \"twl\": %s,\n", hex(blk.arm9Report).c_str(),
	       (r.flags & 1) ? "true" : "false", (r.flags & 2) ? "true" : "false");
	printf("    \"scfg_a9rom\": \"%s\", \"scfg_clk\": \"%s\", \"scfg_ext9_boot\": \"%s\", \"scfg_ext9_now\": \"%s\",\n",
	       hex(r.scfgA9Rom).c_str(), hex(r.scfgClk).c_str(), hex(r.scfgExt9Boot).c_str(), hex(r.scfgExt9Now).c_str());
	printf("    \"mbk\": [");
	for (int i = 0; i < 9; i++) printf("%s\"%s\"", i ? ", " : "", hex(r.mbk[i]).c_str());
	printf("],\n    \"region1_boot\": \"%s\", \"mpu\": [", hex(r.region1Boot).c_str());
	for (int i = 0; i < 8; i++) printf("%s\"%s\"", i ? ", " : "", hex(r.mpu[i]).c_str());
	printf("],\n    \"console_type\": \"%s\",\n    \"arenas\": [", hex(r.consoleType).c_str());
	for (int i = 0; i < dsi::REPORT_ARENAS; i++)
		printf("%s[\"%s\", \"%s\"]", i ? ", " : "", hex(r.arenaLo[i]).c_str(), hex(r.arenaHi[i]).c_str());
	printf("],\n    \"mmem_checker\": [%u, %u],\n", r.mmemChecker9, r.mmemChecker7);
	printf("    \"ntr_header\": \"%s\", \"twl_header\": \"%s\", \"ntr_user\": \"%s\",\n",
	       ascii(r.ntrHeader, 4).c_str(), ascii(r.twlHeader, 4).c_str(), ascii(r.ntrUser, 4).c_str());
	std::vector<u32> work(r.workTicks, r.workTicks + dsi::REPORT_WORK_WINDOW);
	std::sort(work.begin(), work.end());
	printf("    \"extended_ram\": %s, \"fast_clock\": %s, \"frames\": %u, \"work_ticks_median\": %u, \"work_ticks_max\": %u,\n",
	       (r.flags & 4) ? "true" : "false", (r.flags & 8) ? "true" : "false", r.frames, work[work.size() / 2], work.back());
	auto heap = [](const char* name, const dsi::HeapInfo& h, bool last) {
		printf("    \"%s\": {\"heap\": \"%s\", \"start\": \"%s\", \"size\": %u, \"free\": %u, \"free_min\": %u}%s\n",
		       name, hex(h.heap).c_str(), hex(h.start).c_str(), h.size, h.freeNow, h.freeMin, last ? "" : ",");
	};
	heap("root_heap", r.root, false);
	heap("extended_heap", r.extended, false);
	heap("low_heap", r.low, false);
	heap("nwram_heap", r.nwram, true);
	printf("  }\n}\n");
}

} // namespace

int main(int argc, char** argv)
{
	Options o = parse(argc, argv);
	auto nds = makeConsole(o);

	auto rom = readFile(o.rom);
	auto cart = NDSCart::ParseROM(rom.data(), rom.size());
	if (!cart)
		die("cannot parse ROM " + o.rom);

	nds->Reset();
	nds->SetNDSCart(std::move(cart));
	nds->SetupDirectBoot(o.rom);
	nds->Start();

	size_t shot = 0;
	for (u32 frame = 1; frame <= o.frames; frame++) {
		u32 held = 0;
		for (const Press& p : o.presses)
			if (frame >= p.frame && frame < p.frame + p.hold)
				held |= p.mask;
		nds->SetKeyMask(0xFFF & ~held);
		const Touch* touch = nullptr;
		for (const Touch& t : o.touches)
			if (frame >= t.frame && frame < t.frame + t.hold)
				touch = &t;
		if (touch) nds->TouchScreen(touch->x, touch->y);
		else nds->ReleaseScreen();
		nds->RunFrame();
		for (const Shot& s : o.shots)
			if (s.frame == frame) {
				writePPM(*nds, s.path);
				shot++;
			}
		for (const Dump& d : o.dumps)
			if (d.frame == frame) {
				printf("frame %u, %s %08X:", frame, d.arm7 ? "arm7" : "arm9", d.address);
				for (u32 i = 0; i < d.length; i += 4)
					printf(" %08X", d.arm7 ? nds->ARM7Read32(d.address + i) : nds->ARM9Read32(d.address + i));
				printf("\n");
			}
	}

	fprintf(stderr, "harness: %u save write(s)\n", Platform::g_saveWrites);
	if (o.report)
		printReport(*nds);
	return 0;
}
