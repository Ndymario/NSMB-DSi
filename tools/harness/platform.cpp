// Minimal melonDS Platform layer for a headless, single-instance run.
// No networking, camera, microphone or add-ons: the harness only boots ROMs.

#include "Platform.h"
#include "net/LocalMP.h"

#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include <dlfcn.h>
#include <unistd.h>

namespace melonDS::Platform {

int g_logLevel = Warn; // set by main()
unsigned g_saveWrites = 0; // NDS save memory flushes, reported by main()

void SignalStop(StopReason, void*) {}

std::string GetLocalFilePath(const std::string& filename) { return filename; }

FileHandle* OpenFile(const std::string& path, FileMode mode)
{
	if ((mode & (Read | Write)) == None)
		return nullptr;
	if ((mode & NoCreate) && access(path.c_str(), F_OK) != 0)
		return nullptr;

	// Preserve keeps an existing file's contents but still creates a missing
	// one (melonDS creates its SD card image this way).
	if ((mode & Preserve) && !(mode & NoCreate) && access(path.c_str(), F_OK) != 0)
		mode = static_cast<FileMode>(mode & ~Preserve);

	const char* m;
	if (mode & Append)                  m = (mode & Read) ? "a+b" : "ab";
	else if ((mode & ReadWrite) == ReadWrite) m = (mode & Preserve) ? "r+b" : "w+b";
	else if (mode & Write)              m = (mode & Preserve) ? "r+b" : "wb";
	else                                m = "rb";
	return reinterpret_cast<FileHandle*>(fopen(path.c_str(), m));
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode) { return OpenFile(path, mode); }
bool FileExists(const std::string& name) { return access(name.c_str(), F_OK) == 0; }
bool LocalFileExists(const std::string& name) { return FileExists(name); }
bool CheckFileWritable(const std::string& path) { return access(path.c_str(), W_OK) == 0 || !FileExists(path); }
bool CheckLocalFileWritable(const std::string& path) { return CheckFileWritable(path); }

static FILE* F(FileHandle* f) { return reinterpret_cast<FILE*>(f); }
bool CloseFile(FileHandle* f) { return fclose(F(f)) == 0; }
bool IsEndOfFile(FileHandle* f) { return feof(F(f)) != 0; }
bool FileReadLine(char* str, int count, FileHandle* f) { return fgets(str, count, F(f)) != nullptr; }
u64 FilePosition(FileHandle* f) { return ftello(F(f)); }
bool FileSeek(FileHandle* f, s64 offset, FileSeekOrigin origin)
{
	int whence = origin == FileSeekOrigin::Start ? SEEK_SET : origin == FileSeekOrigin::Current ? SEEK_CUR : SEEK_END;
	return fseeko(F(f), offset, whence) == 0;
}
void FileRewind(FileHandle* f) { rewind(F(f)); }
u64 FileRead(void* data, u64 size, u64 count, FileHandle* f) { return fread(data, size, count, F(f)); }
bool FileFlush(FileHandle* f) { return fflush(F(f)) == 0; }
u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* f) { return fwrite(data, size, count, F(f)); }
u64 FileWriteFormatted(FileHandle* f, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int n = vfprintf(F(f), fmt, args);
	va_end(args);
	return n < 0 ? 0 : n;
}
u64 FileLength(FileHandle* f)
{
	off_t pos = ftello(F(f));
	fseeko(F(f), 0, SEEK_END);
	off_t len = ftello(F(f));
	fseeko(F(f), pos, SEEK_SET);
	return len;
}

// The module's own debug lines start with "[DSi]" and are always shown.
void Log(LogLevel level, const char* fmt, ...)
{
	char line[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	if (level >= g_logLevel || strncmp(line, "[DSi]", 5) == 0)
		fputs(line, stderr);
}

struct Thread { std::thread t; };
Thread* Thread_Create(std::function<void()> func) { return new Thread{std::thread(std::move(func))}; }
void Thread_Free(Thread* t) { if (t->t.joinable()) t->t.detach(); delete t; }
void Thread_Wait(Thread* t) { if (t->t.joinable()) t->t.join(); }

struct Semaphore { std::mutex m; std::condition_variable cv; int count = 0; };
Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* s) { delete s; }
void Semaphore_Reset(Semaphore* s) { std::lock_guard l(s->m); s->count = 0; }
void Semaphore_Wait(Semaphore* s) { std::unique_lock l(s->m); s->cv.wait(l, [s] { return s->count > 0; }); s->count--; }
bool Semaphore_TryWait(Semaphore* s, int timeout_ms)
{
	std::unique_lock l(s->m);
	if (!s->cv.wait_for(l, std::chrono::milliseconds(timeout_ms), [s] { return s->count > 0; }))
		return false;
	s->count--;
	return true;
}
void Semaphore_Post(Semaphore* s, int count) { { std::lock_guard l(s->m); s->count += count; } s->cv.notify_all(); }

struct Mutex { std::mutex m; };
Mutex* Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex* m) { delete m; }
void Mutex_Lock(Mutex* m) { m->m.lock(); }
void Mutex_Unlock(Mutex* m) { m->m.unlock(); }
bool Mutex_TryLock(Mutex* m) { return m->m.try_lock(); }

void Sleep(u64 usecs) { std::this_thread::sleep_for(std::chrono::microseconds(usecs)); }
static auto g_start = std::chrono::steady_clock::now();
u64 GetMSCount() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_start).count(); }
u64 GetUSCount() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - g_start).count(); }

void WriteNDSSave(const u8*, u32, u32, u32, void*) { g_saveWrites++; }
void WriteGBASave(const u8*, u32, u32, u32, void*) {}
void WriteFirmware(const Firmware&, u32, u32, void*) {}
void WriteDateTime(int, int, int, int, int, int, void*) {}

// Local wireless between the consoles of one --mp run; userdata is the instance.
static LocalMP& mp() { static LocalMP local; return local; }
static int inst(void* userdata) { return static_cast<int>(reinterpret_cast<intptr_t>(userdata)); }
void MP_Begin(void* u) { mp().Begin(inst(u)); }
void MP_End(void* u) { mp().End(inst(u)); }
int MP_SendPacket(u8* d, int len, u64 ts, void* u) { return mp().SendPacket(inst(u), d, len, ts); }
int MP_RecvPacket(u8* d, u64* ts, void* u) { return mp().RecvPacket(inst(u), d, ts); }
int MP_SendCmd(u8* d, int len, u64 ts, void* u) { return mp().SendCmd(inst(u), d, len, ts); }
int MP_SendReply(u8* d, int len, u64 ts, u16 aid, void* u) { return mp().SendReply(inst(u), d, len, ts, aid); }
int MP_SendAck(u8* d, int len, u64 ts, void* u) { return mp().SendAck(inst(u), d, len, ts); }
int MP_RecvHostPacket(u8* d, u64* ts, void* u) { return mp().RecvHostPacket(inst(u), d, ts); }
u16 MP_RecvReplies(u8* d, u64 ts, u16 mask, void* u) { return mp().RecvReplies(inst(u), d, ts, mask); }
int Net_SendPacket(u8*, int, void*) { return 0; }
int Net_RecvPacket(u8*, void*) { return 0; }

void Camera_Start(int, void*) {}
void Camera_Stop(int, void*) {}
void Camera_CaptureFrame(int, u32* frame, int width, int height, bool, void*)
{
	for (int i = 0; i < width * height; i++) frame[i] = 0;
}

void Mic_Start(void*) {}
void Mic_Stop(void*) {}
int Mic_ReadInput(s16*, int, void*) { return 0; }

AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder*) {}
bool AAC_Configure(AACDecoder*, int, int) { return false; }
bool AAC_DecodeFrame(AACDecoder*, const void*, int, void*, int) { return false; }

bool Addon_KeyDown(KeyType, void*) { return false; }
void Addon_RumbleStart(u32, void*) {}
void Addon_RumbleStop(void*) {}
float Addon_MotionQuery(MotionQueryType, void*) { return 0.f; }

DynamicLibrary* DynamicLibrary_Load(const char* lib) { return reinterpret_cast<DynamicLibrary*>(dlopen(lib, RTLD_NOW)); }
void DynamicLibrary_Unload(DynamicLibrary* lib) { dlclose(lib); }
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name) { return dlsym(lib, name); }

} // namespace melonDS::Platform
