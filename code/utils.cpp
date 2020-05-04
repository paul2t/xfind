
internal_function umm getFileSize(const char* filename)
{
    memid result = 0;
    FILE* file = fopen(filename, "rb");
    if (file)
    {
        _fseeki64(file, 0, SEEK_END);
        result = _ftelli64(file);
        fclose(file);
    }
    return result;
}

internal_function umm getFileSize(HANDLE file)
{
    DWORD high = 0;
    umm result = GetFileSize(file, &high);
    result = result | (umm)high << 32;
    return result;
}

// NOTE(xf4): A 0 is appended at the end of the buffer.
internal_function String readEntireFile(const char* filename)
{
	String result = {};

	FILE* file = fopen(filename, "rb");
	if (file)
	{
		_fseeki64(file, 0, SEEK_END);
		memid filesize = _ftelli64(file);
		_fseeki64(file, 0, SEEK_SET);
		char* buffer = (char*)malloc(sizeof(char) * filesize + 1);
		memid nitemsRead = fread(buffer, 1, filesize, file);
		if (nitemsRead == filesize)
		{
			result.str = buffer;
			result.size = (i32)filesize;
			result.memory_size = (i32)result.size + 1;
			buffer[filesize] = 0;
		}
		else
		{
			free(buffer);
			buffer = 0;
		}
		fclose(file);
	}

	return result;
}

// NOTE(xf4): A 0 is appended at the end of the buffer.
internal_function String readEntireFile(MemoryArena& arena, const char* filename)
{
	String result = {};

	FILE* file = fopen(filename, "rb");
	if (file)
	{
		TempMemory tempMem(arena);
		_fseeki64(file, 0, SEEK_END);
		memid filesize = _ftelli64(file);
		_fseeki64(file, 0, SEEK_SET);
		char* buffer = pushArray(arena, char, filesize + 1, pushpNoClear());
		memid nitemsRead = fread(buffer, 1, filesize, file);
		if (nitemsRead > 0)
		{
			result.str = buffer;
			result.size = (i32)nitemsRead;
			result.memory_size = (i32)result.size+1;
			buffer[nitemsRead] = 0;
			tempMem.commit();
		}
		fclose(file);
	}

	return result;
}

inline void advance_char(String& s)
{
	if (s.size > 0)
	{
		++s.str;
		--s.size;
		if (s.memory_size > 0)
			--s.memory_size;
	}
}

inline void advance_char(String& s, char c)
{
	if (s.size > 0 && s.str[0] == c)
	{
		++s.str;
		--s.size;
		if (s.memory_size > 0)
			--s.memory_size;
	}
}

inline String firstLine(String s)
{
	String line = {};
	line.str = s.str;
	line.memory_size = s.size;
	while (line.size < line.memory_size && line.str[line.size] != '\r' && line.str[line.size] != '\n') ++line.size;
	return line;
}

inline String nextLine(String prevline)
{
	if (prevline.memory_size <= prevline.size) return {};
	String line = {};
	line.str = prevline.str + prevline.size;
	line.memory_size = prevline.memory_size - prevline.size;
	line.size = 2;
	if (line.memory_size < 2)
		line.size = line.memory_size;
	advance_char(line, '\r');
	advance_char(line, '\n');
	line.size = 0;

	// Find the end of the line
	while (line.size < line.memory_size && line.str[line.size] != '\r' && line.str[line.size] != '\n') ++line.size;

	return line;
}

internal_function String* getLines(MemoryArena& arena, String content, i32& nbLines, bool skipWS = false, bool chopWS = false, bool nullTerminate = false)
{
	i32 lineCount = 1;
	for (i32 i = 0; i < content.size; ++i)
	{
		if (content.str[i] == '\n')
			++lineCount;
	}
	String* result = pushArray(arena, String, lineCount + 1);
	String at = content;
	for (i32 li = 0; li < lineCount; ++li)
	{
		i32 newlineI = find(at, 0, '\n');
		bool isWindowsLine = at.str[newlineI - 1] == '\r';
		if (isWindowsLine)
			--newlineI;

		result[li] = substr(at, 0, newlineI);
		if (skipWS)
			result[li] = skip_whitespace(result[li]);
		if (chopWS)
			result[li] = chop_whitespace(result[li]);
		if (nullTerminate)
			result[li].str[result[li].size] = 0;

		if (isWindowsLine)
			++newlineI;
		at = substr(at, newlineI + 1);
	}
	nbLines = lineCount;
	return result;
}

internal_function String getWord(String s, String* remaining = 0)
{
	String result = {};
	String line = skip_whitespace(s);
	i32 i = 0;
	for (; i < line.size; ++i)
	{
		if (char_is_whitespace(line.str[i]))
		{
			result = substr(line, 0, i);
			if (remaining) *remaining = skip_whitespace(substr(line, i));
			break;
		}
	}
	if (i >= line.size)
	{
		result = line;
		if (remaining) remaining->size = 0;
	}
	return result;
}

#define strexp(s) (s).size, (s).str

inline String pushNewString(MemoryArena& arena, int len)
{
    String result = make_string_cap(pushArray(arena, char, len), 0, len);
    return result;
}

inline String pushString(MemoryArena& arena, String s)
{
    String result = make_string_cap(pushCopy(arena, char, s.size, s.str), s.size, s.size);
    return result;
}

inline String pushStringZeroTerminated(MemoryArena& arena, String s, PushParam param = pushpDefault())
{
    param.flags = flagUnset(param.flags, PushParamFlags_ClearToZero);
    String result = make_string_cap(pushArray(arena, char, s.size+1, param), 0, s.size+1);
    append(&result, s);
    terminate_with_null(&result);
    return result;
}


internal_function i32 splitByChar(String* outputs, i32 outputsMaxSize, char* input, char c)
{
	i32 outputsSize = 0;
	char* at = input;
	String current = {};
	current.str = at;
	while (*at)
	{
		if (*at == c)
		{
			if (current.size > 0)
				outputs[outputsSize++] = current;
			++at;
			current = {};
			current.str = at;
			if (outputsSize >= outputsMaxSize)
				break;
		}
		else
		{
			++current.size;
			++current.memory_size;
			++at;
		}
	}
	if (current.size > 0)
		outputs[outputsSize++] = current;
	return outputsSize;
}


internal_function b32 findStringInArrayInsensitive(String* list, i32 listSize, String str)
{
	b32 found = false;
	for (i32 di = 0; di < listSize; ++di)
	{
		if (match_insensitive(list[di], str))
		{
			found = true;
			break;
		}
	}
	return found;
}


// NOTE(xf4): This is 6 times faster than strtol
internal_function u32 fastStringToU32(char* str, char*& end)
{
    u32 r = 0;
    char* at = str;
    
    while ('0' <= *at && *at <= '9')
    {
        r = r * 10 + *at - '0';
        ++at;
    }
    
    end = at;
    return r;
}

// @return true if succeeded
internal_function bool copyToClipboard(const char* str, int size)
{
    bool result = false;
    if (OpenClipboard(0))
    {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, size+1);
        
        if (hg)
        {
            memcpy(GlobalLock(hg), str, size);
            GlobalUnlock(hg);
            SetClipboardData(CF_TEXT, hg);
        }
        
        CloseClipboard();
        GlobalFree(hg);
    }
    return result;
}

internal_function bool copyToClipboard(String s)
{
    return copyToClipboard(s.str, s.size);
}




void execProgram(char* program)
{
	STARTUPINFO infos = {};
	infos.cb = sizeof(STARTUPINFO);
	PROCESS_INFORMATION pinfos = {};
	CreateProcessA(0, program, 0, 0, FALSE, 0, 0, 0, &infos, &pinfos);
	CloseHandle(pinfos.hThread);
	CloseHandle(pinfos.hProcess);
}


struct MutexRW
{
    volatile i32 read = 0;
    volatile i32 write = 0;
};

inline b32 TryLockMutex(volatile i32* mutex)
{
	return InterlockedCompareExchange((LONG volatile*)mutex, 1, 0);
}

inline void LockMutex(volatile i32* mutex)
{
	while (!TryLockMutex(mutex));
    _WriteBarrier();
}

inline void UnlockMutex(volatile i32* mutex)
{
    _WriteBarrier();
    *mutex = 0;
}

inline b32 TryLockMutexRead(MutexRW* mutex)
{
	if (TryLockMutex(&mutex->write))
	{
		InterlockedAdd((volatile LONG*)&mutex->read, 1);
		UnlockMutex(&mutex->write);
		return true;
	}
	return false;
}

inline void LockMutexRead(MutexRW* mutex)
{
    LockMutex(&mutex->write);
	InterlockedAdd((volatile LONG*)&mutex->read, 1);
    UnlockMutex(&mutex->write);
}

inline void UnlockMutexRead(MutexRW* mutex)
{
    _WriteBarrier();
	InterlockedAdd((volatile LONG*)&mutex->read, -1);
    assert(mutex->read >= 0);
}

inline b32 TryLockMutexWrite(MutexRW* mutex, i32 expectedReads = 0)
{
	if (TryLockMutex(&mutex->write))
	{
		if (mutex->read <= expectedReads)
			return true;
		UnlockMutex(&mutex->write);
	}
	return false;
}

inline void LockMutexWrite(MutexRW* mutex, i32 expectedReads = 0)
{
    LockMutex(&mutex->write);
    while (mutex->read > expectedReads) {};
}

inline void UnlockMutexWrite(volatile MutexRW* mutex)
{
    UnlockMutex(&mutex->write);
}


#ifdef __cplusplus

struct _ScopeMutex
{
    volatile i32* mutex;
    _ScopeMutex(volatile i32* mutex) : mutex(mutex) { LockMutex(mutex); }
    ~_ScopeMutex() { UnlockMutex(mutex); }
};

#define CONCAT_(a, b) a ## b
#define CONCAT(a, b) CONCAT_(a, b)
#define ScopeMutex(mutex) _ScopeMutex CONCAT(mutex_, __LINE__)(mutex)

struct _ScopeMutexRead
{
    MutexRW* mutex;
    _ScopeMutexRead(MutexRW* mutex) : mutex(mutex) { LockMutexRead(mutex); }
    ~_ScopeMutexRead() { UnlockMutexRead(mutex); }
};
#define ScopeMutexRead(mutex) _ScopeMutexRead CONCAT(mutex_, __LINE__)(mutex)

struct _ScopeMutexWrite
{
    MutexRW* mutex;
    _ScopeMutexWrite(MutexRW* mutex, u32 expectedReads = 0) : mutex(mutex) { LockMutexWrite(mutex, expectedReads); }
    ~_ScopeMutexWrite() { UnlockMutexWrite(mutex); }
};
#define ScopeMutexWrite(mutex, ...) _ScopeMutexWrite CONCAT(mutex_, __LINE__)(mutex, ##__VA_ARGS__)







// Atomic list

template <typename T>
inline void AtomicListAppend(T**& listEnd, T* element)
{
	volatile T** list = *(volatile T***)&listEnd;
	while (InterlockedCompareExchange64((volatile LONG64*)&listEnd, (LONG64)&element->next, (LONG64)list) != (LONG64)list)
		list = *(volatile T***)&listEnd;
	*list = element;
}

template <typename T>
inline T* AtomicListPopFirst(T*& list)
{
	T* entry = (T*)*(volatile T**)&list;
	while (entry && InterlockedCompareExchange64((volatile LONG64*)&list, (LONG64)entry->next, (LONG64)entry) != (LONG64)entry)
		entry = (T*)*(volatile T**)&list;
	return entry;
}

template <typename T>
inline void AtomicListInsert(T*& list, T* element, T*&next)
{
	next = (T*)*(volatile T**)&list;
	while (InterlockedCompareExchange64((volatile LONG64*)&list, (LONG64)element, (LONG64)next) != (LONG64)next)
		next = (T*)*(volatile T**)&list;
}

#endif


//
// Perfos
//

#if APP_INTERNAL




enum ProfileType
{
    ProfileType_Unknown,

    ProfileType_FrameMarker,
    ProfileType_BlockBegin,
    ProfileType_BlockEnd,

    ProfileType_NoMoreProfileEvent,
};

struct ProfileEvent
{
    u64 clock;
    char* name;
    char* file;
    int line;
    u32 count;
    u8 type;
};

struct ProfileState
{
    u32 maxEventCount;
    u32 eventCount;
    ProfileEvent* events;
    MemoryArena profileArena;
    FILE* output = stdout;
    FILE* dump = 0;
    u64 startTime;
    u64 lastPrintTime;
    i32 depth;
};

extern thread_local ProfileState mainProfileState;

#define DEBUG_EVENT(type, name, ...) recordProfileEvent(&mainProfileState, __FILE__, __LINE__, type, name, ##__VA_ARGS__)
#define TIMED_BLOCK_BEGIN(name) DEBUG_EVENT(ProfileType_BlockBegin, name)
#define TIMED_BLOCK_END(name, ...) DEBUG_EVENT(ProfileType_BlockEnd, name, ##__VA_ARGS__)
#define TIMED_BLOCK_END_WITH_PROFILE(name, ...) DEBUG_EVENT(ProfileType_BlockEnd, name, 1, true, ##__VA_ARGS__)
#define TIMED_BLOCK(name, ...) TimedBlock CONCAT(_timed_block_, __COUNTER__)(&mainProfileState, name, __FILE__, __LINE__, ##__VA_ARGS__)
#define TIMED_BLOCK_WITH_PROFILE(name, ...) TIMED_BLOCK(name, 1, true, ##__VA_ARGS__)
#define TIMED_FUNCTION(...) TIMED_BLOCK(__FUNCTION__, ##__VA_ARGS__)
#define TIMED_FUNCTION_WITH_PROFILE(...) TIMED_BLOCK_WITH_PROFILE(__FUNCTION__, ##__VA_ARGS__)
// NOTE(xf4): You can do : TIMED_BLOCK/TIMED_FUNCTION(count, showProfile, output);

#define MUTE_PROFILE() TIMED_BLOCK("MUTE_PROFILE", 1, false, true)


void recordProfileEvent(ProfileState* profileState, char* file, int line, ProfileType type, char* name, u32 count = 1, bool showProfile = false, bool muteProfile = false, FILE* output = 0);

void printProfile(ProfileState* profileState = &mainProfileState, bool mergeLastBlock = false, FILE* output = 0, bool muteProfile = false);

inline void resetProfileState(ProfileState* profileState = &mainProfileState)
{
    profileState->eventCount = 0;
}




struct TimedBlock
{
    ProfileState* profileState = 0;
    char* name = 0;
    char* file = 0;
    int line = 0;
    int count = 0;
    bool show_profile = false;
    bool mute_profile = false;

    inline TimedBlock(ProfileState* profileState, char* name, char* file, int line, int count = 1, bool show_profile = false, bool mute_profile = false) :
        profileState(profileState), name(name), count(count), file(file), line(line), show_profile(show_profile), mute_profile(mute_profile)
    {
        if (!profileState || !profileState->maxEventCount) return;
        if (profileState->eventCount == 0)
            this->show_profile = true;
        recordProfileEvent(profileState, file, line, ProfileType_BlockBegin, name);
    }
    inline ~TimedBlock()
    {
        if (!profileState || !profileState->maxEventCount) return;
        recordProfileEvent(profileState, file, line, ProfileType_BlockEnd, name, count, show_profile, mute_profile);
    }
};



inline u64 getTicksPerSecond()
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    u64 result = freq.QuadPart;
    return result;
}
static u64 qpcTicksPerSecond = getTicksPerSecond();
inline u64 getTickCount()
{
    LARGE_INTEGER time = {};
    QueryPerformanceCounter(&time);
    u64 result = time.QuadPart;
    result *= 1000000;
    result /= qpcTicksPerSecond;
    return result;
}


#define MEMSTR_FORMAT "%.1lf "
#define CYSTR_FORMAT "%6.2lf "


static char* memstr[] =
{
    MEMSTR_FORMAT " B", // Bytes
    MEMSTR_FORMAT "KB", // Kilobytes
    MEMSTR_FORMAT "MB", // Megabytes
    MEMSTR_FORMAT "GB", // Gigabytes
    MEMSTR_FORMAT "TB", // Terabytes
    MEMSTR_FORMAT "PB", // Petabytes
    MEMSTR_FORMAT "EB", // Exabytes
    MEMSTR_FORMAT "ZB", // Zettabytes
    MEMSTR_FORMAT "YB", // Yottabytes
    MEMSTR_FORMAT "XB", // Xenottabytes
    MEMSTR_FORMAT "SB", // Shilentnobytes
    MEMSTR_FORMAT "DB", // Domegemegrottebytes
};

static char* cystr[] =
{
    CYSTR_FORMAT " cy",
    CYSTR_FORMAT "Kcy",
    CYSTR_FORMAT "Mcy",
    CYSTR_FORMAT "Gcy",
    CYSTR_FORMAT "Tcy",
    CYSTR_FORMAT "Pcy",
    CYSTR_FORMAT "Ecy",
    CYSTR_FORMAT "Zcy",
    CYSTR_FORMAT "Ycy",
    CYSTR_FORMAT "Xcy",
    CYSTR_FORMAT "Scy",
    CYSTR_FORMAT "Dcy",
};


internal_function char* memToStr(umm size, char* buffer)
{
    int pwr10 = 0;
    double result = (double)size;
    while (result >= 1024.0)
    {
        result /= 1024;
        ++pwr10;
    }
    sprintf(buffer, memstr[pwr10], result);
    return buffer;
}

internal_function char* cyToStr(umm size, char* buffer)
{
    int pwr10 = 0;
    double result = (double)size;
    while (result >= 1000.0)
    {
        result /= 1000;
        ++pwr10;
    }
    sprintf(buffer, cystr[pwr10], result);
    return buffer;
}

internal_function char* microsecondsToStr(u64 microseconds, char* buffer)
{
    if (microseconds < 1000)
    {
        sprintf(buffer, "   %3d us", (int)microseconds);
    }
    else if (microseconds < 10000)
    {
        f32 fms = ((f32)microseconds) / 1000;
        sprintf(buffer, "   %2.1f ms", fms);
    }
    else
    {
        u64 milliseconds = microseconds / 1000;
        if (milliseconds < 1000)
        {
            sprintf(buffer, "   %3d ms", (int)milliseconds);
        }
        else
        {
            u64 seconds = milliseconds / 1000;

            if (seconds < 60)
            {
                u32 remaining_centiseconds = (milliseconds % 1000) / 10;
                sprintf(buffer, "  %2d.%02d s", (int)seconds, remaining_centiseconds);
            }
            else
            {
                u64 minutes = seconds / 60;
                if (minutes < 60)
                {
                    int remaining_seconds = seconds % 60;
                    sprintf(buffer, "  %2d:%02d m", (int)minutes, remaining_seconds);
                }
                else
                {
                    u64 hours = minutes / 60;
                    int remaining_minutes = minutes % 60;
                    sprintf(buffer, "%4d:%02d h", (int)hours, remaining_minutes);
                }
            }
        }
    }
    return buffer;
}

#define timeToStr(time, buffer) microsecondsToStr(time, buffer)



static int countNbChars(int number)
{
    int nbChars = number >= 0 ? 1 : 2;
    while (number >= 10)
    {
        ++nbChars;
        number = number / 10;
    }
    return nbChars;
}

#define CYCLE_HASH_SIZE 4096
inline u32 getCycleHashValue(ProfileEvent event)
{
    u32 result = event.line;
    int i = 0;
    char* file = event.file;
    for (char* at = file; *at; ++at)
    {
        result = (7 * i * result + 13 * (*at)) % CYCLE_HASH_SIZE;
        ++i;
    }
    return result;
}

ProfileState initProfileState()
{
    ProfileState result = {};
    {
        result.maxEventCount = 512 * 65536;
        result.events = pushArray(result.profileArena, ProfileEvent, result.maxEventCount + 1, pushpNoClear());
		static const char* dumpOutput = 0; // "dump_profile.txt";
        if (dumpOutput)
        {
			{
				volatile static i32 creation_mutex = false;
				volatile static i32 file_created = false;
				ScopeMutex(&creation_mutex);
				if (!file_created)
				{
					result.dump = fopen(dumpOutput, "wb");
					if (result.dump)
					{
						//fprintf(result.dump, "{\"traceEvents\":[");
						fprintf(result.dump, "{\"traceEvents\":[\n]}\n");
						//fprintf(result.dump, "profile clock type merge count line name file\0");
						fflush(result.dump);
					}
					file_created = true;
				}
				else
					result.dump = fopen(dumpOutput, "a+b");
			}
        }
        result.lastPrintTime = 0;
        result.startTime = getTickCount();
    }
    return result;
}
thread_local ProfileState mainProfileState = initProfileState();


struct CycleCount
{
    u32 hitCount;
    u64 time;
    u64 timeOfChildren;
    char* name;
    char* file;
    int line;
    CycleCount* nextInHash;
};

struct ProfileFrame
{
    ProfileEvent* start;
    u64 timeOfChildren;
};

void recordProfileEvent(ProfileState* profileState, char* file, int line, ProfileType type, char* name, u32 count, bool showProfile, bool muteProfile, FILE* output)
{
	if (!profileState->maxEventCount) return;
    u64 clock = getTickCount() - profileState->startTime;
	thread_local i32 threadid = GetCurrentThreadId();
	u32 event_index = (u32)-1;
	if (profileState->eventCount < profileState->maxEventCount)
		 event_index = (u32)InterlockedIncrement((volatile LONG*)&profileState->eventCount) - 1;
	if (event_index < profileState->maxEventCount)
    {
		ProfileEvent* event = profileState->events + event_index;
        event->clock = clock;
        event->name = name;
        event->file = file;
        event->line = line;
        event->count = count;
        event->type = (u8)type;
    }
    else
    {
        local_persist bool printed = false;
        if (!printed)
        {
            printed = true;
            fprintf(stderr, "WARNING: Profile stack is full !!!\n");
        }
    }

    FILE* dump = profileState->dump;
    if (dump)
    {
#if 0
        u16 file_len = (u16)strlen(file);
        u16 name_len = (u16)strlen(name);
        u16 len = sizeof(u64) + sizeof(u8) + sizeof(u8) + sizeof(u32) + sizeof(i32);
        len += name_len + 1 + file_len + 1;
        fwrite(&len, sizeof(u16), 1, dump);

        fwrite(&clock, sizeof(u64), 1, dump);
        fwrite(&type, sizeof(u8), 1, dump);
        fwrite(&showProfile, sizeof(u8), 1, dump);
        fwrite(&muteProfile, sizeof(u8), 1, dump);
        fwrite(&count, sizeof(u32), 1, dump);
        fwrite(&line, sizeof(i32), 1, dump);
        //fwrite(&name, sizeof(char*), 1, dump);
        //fwrite(&file, sizeof(char*), 1, dump);
        fwrite(name, sizeof(char), name_len + 1, dump);
        fwrite(file, sizeof(char), file_len + 1, dump);
		fflush(dump);
#elif 0
        fprintf(dump, "%I64u\t%d\t%u\t%d\t%d\t%s\t%d\t%s\n", clock, (int)type, (int)showProfile, (int)muteProfile, count, line, name, file);
		fflush(dump);
#else
        char event_char = ' ';
        if (type == ProfileType_BlockBegin)
            event_char = 'B';
        else if (type == ProfileType_BlockEnd)
            event_char = 'E';
        else if (type == ProfileType_FrameMarker)
            event_char = 'F';
        else
            assert(0);

        volatile static i32 sfirst = true;
		u32 first = sfirst;
		//if (first)
			//first = (u32)InterlockedCompareExchange((volatile LONG*)&sfirst, 0, 1);

        {
			//static i32 mutex = 0;
			//ScopeMutex(&mutex);

            fseek(dump, -4, SEEK_END);
            if (!first)
                fwrite(",", 1, 1, dump);
            //fprintf(dump, "\n{\"cat\":\"Application\",\"pid\":0,\"tid\":%d,\"ts\":%I64u,\"ph\":\"%c\",\"name\":\"%s\",\"args\":{}},", threadid, clock / 1000, event_char, name);
            fprintf(dump, "\n{\"cat\":\"Application\",\"pid\":0,\"tid\":%d,\"ts\":%I64u,\"ph\":\"%c\",\"name\":\"%s\",\"args\":{}}\n]}\n", threadid, clock / 1000, event_char, name);
            if (first)
                first = false;
			//fflush(dump);
		}
#endif
    }

    if (type == ProfileType_BlockBegin)
        profileState->depth++;
    else if (type == ProfileType_BlockEnd)
    {
        profileState->depth--;
        if (profileState->depth == 0 || showProfile)
        {
            printProfile(profileState, true, output, muteProfile);
        }

        if (profileState->depth < 0)
        {
            fprintf(stderr, "ERROR: Profile stack is negative: Too many END EVENTs\n");
            profileState->depth = 0;
        }
    }
    else if (type == ProfileType_FrameMarker)
    {
        printProfile(profileState, true, output, muteProfile);
        resetProfileState(profileState);
    }

}


void printProfile(ProfileState* profileState, bool mergeLastBlock, FILE* output, bool muteProfile)
{
    // NOTE(xf4): Do not use TIMED_BLOCK in this function

    if (profileState->eventCount <= 0) return;
    if (!output)
        output = profileState->output;

    TempMemory temp(profileState->profileArena);
    i32 stackSize = 0;
    ProfileFrame stack[1024];
    CycleCount** cycleHashTable = pushArray(profileState->profileArena, CycleCount*, CYCLE_HASH_SIZE);
    ProfileFrame* root = stack + stackSize++;
    ZeroStruct(*root);
    u64 totalTime = 0;
    i32 rootCount = 0;
    u32 events_to_skip = 0;
    i32 event_count = 0;
    u64 untimed_range = 0;

    {
        root->start = profileState->events;
        root->timeOfChildren = 0;

        u32 it = profileState->eventCount - 1;
        i32 depth = 0;
        while (it > 0)
        {
            ProfileEvent* event = profileState->events + it;
            if (event->type == ProfileType_BlockBegin)
            {
                --depth;
                if (depth <= 0)
                    break;
            }
            else if (event->type == ProfileType_BlockEnd)
                ++depth;

            --it;
        }
        events_to_skip = it;

        event_count = profileState->eventCount - events_to_skip;
        ProfileEvent first = profileState->events[events_to_skip];
        ProfileEvent last = profileState->events[profileState->eventCount - 1];
        totalTime = last.clock - first.clock;

        if (events_to_skip == 0)
        {
            untimed_range = first.clock - profileState->lastPrintTime;
            profileState->lastPrintTime = last.clock;
        }
    }

    for (u32 ei = events_to_skip; ei < profileState->eventCount; ++ei)
    {
        ProfileEvent* event = profileState->events + ei;
        if (event->type == ProfileType_BlockBegin)
        {
            if (stackSize == 1)
                rootCount++;
            ProfileFrame* frame = stack + stackSize++;
            frame->start = event;
            frame->timeOfChildren = 0;
        }
        else if (event->type == ProfileType_BlockEnd)
        {
            ProfileFrame frame = stack[--stackSize];
            ProfileEvent start = *frame.start;
            if (0 != strcmp(event->name, start.name))
            {
                fprintf(stderr, "WARNING: Begin/End event names not matching: \"%s\" and \"%s\"\n", start.name, event->name);
            }
            u32 hashvalue = getCycleHashValue(start);
            CycleCount* ccout = cycleHashTable[hashvalue];
            while (ccout)
            {
                if (ccout->line == start.line && 0 == strcmp(ccout->file, start.file) && 0 == strcmp(ccout->name, start.name))
                    break;
                ccout = ccout->nextInHash;
            }
            if (!ccout)
            {
                ccout = pushStruct(profileState->profileArena, CycleCount);
                ccout->name = start.name;
                ccout->file = start.file;
                ccout->line = start.line;
                ccout->nextInHash = cycleHashTable[hashvalue];
                cycleHashTable[hashvalue] = ccout;
            }
            ccout->hitCount += event->count;
            u64 time = (event->clock - start.clock);
            ccout->time += time;
            ccout->timeOfChildren += frame.timeOfChildren;
            if (stackSize > 0)
            {
                ProfileFrame* parent = stack + stackSize - 1;
                parent->timeOfChildren += time;
            }
        }
        else
        {
            assert(0); // "Unknown block type";
        }
    }
    if (stackSize != 1)
    {
        fprintf(stderr, "WARNING: perfos stacksize = %d\n", stackSize);
        for (int i = stackSize - 1; i > 0; --i)
        {
            fprintf(stderr, "stack[%d] = %s\n", i, stack[i].start->name);
        }
    }

    if (mergeLastBlock && events_to_skip + 1 < profileState->eventCount - 1)
    {
        if (events_to_skip)
        {
            // NOTE(xf4): We remove all events inside the last closed event.
            profileState->events[events_to_skip + 1] = profileState->events[profileState->eventCount - 1];
            profileState->eventCount = events_to_skip + 2;
        }
        else
        {
            profileState->eventCount = 0;
        }
    }


    if (!output || totalTime <= 0 || muteProfile) return;

    bool needsRoot = rootCount > 1;
    int count = needsRoot ? 1 : 0; // NOTE(xf4): Because we add a root to the list if there are more than one root.
    for (int h = 0; h < CYCLE_HASH_SIZE; ++h)
    {
        CycleCount* ccount = cycleHashTable[h];
        while (ccount)
        {
            ++count;
            ccount = ccount->nextInHash;
        }
    }

    CycleCount** ccounts = pushArray(profileState->profileArena, CycleCount*, count, pushpNoClear());
    int cindex = 0;
    if (needsRoot && root->start)
    {
        CycleCount* ccount = ccounts[cindex++] = pushStruct(profileState->profileArena, CycleCount);
        ProfileEvent* event = root->start;
        ccount->name = "#GlobalScope#";
        ccount->file = event->file;
        ccount->line = event->line;
        ccount->time = totalTime;
        ccount->timeOfChildren = root->timeOfChildren;
        ccount->hitCount = 1;
    }
    for (int h = 0; h < CYCLE_HASH_SIZE; ++h)
    {
        CycleCount* ccount = cycleHashTable[h];
        while (ccount)
        {
            ccounts[cindex++] = ccount;
            ccount = ccount->nextInHash;
        }
    }

    for (int outer = 0; outer < count - 1; ++outer)
    {
        bool sorted = true;
        for (int inner = 0; inner < count - 1; ++inner)
        {
            CycleCount* a = ccounts[inner];
            CycleCount* b = ccounts[inner + 1];
            if (a->time - a->timeOfChildren < b->time - b->timeOfChildren)
            {
                sorted = false;
                CycleCount tmp = *a;
                *a = *b;
                *b = tmp;
            }
        }
        if (sorted)
            break;
    }

    char buffer[1024];
    //fprintf(output, "Global scope: %s (%.2f%%)\n", timeToStr(totalTime-root->timeOfChildren, buffer), (float)(totalTime-root->timeOfChildren) / totalTime * 100);
    int fileMaxLen = 0;
    int lineMaxLen = 0;
    int nameMaxLen = 0;
    for (int h = 0; h < count; ++h)
    {
        CycleCount* ccount = ccounts[h];
        int fileLen = (int)strlen(ccount->file);
        if (fileMaxLen < fileLen)
            fileMaxLen = fileLen;
        int lineLen = countNbChars(ccount->line);
        if (lineMaxLen < lineLen)
            lineMaxLen = lineLen;
        int nameLen = (int)strlen(ccount->name);
        if (nameMaxLen < nameLen)
            nameMaxLen = nameLen;
    }

#define PADDING_STR(len, maxlen) for (int i = 0; i < maxlen - len; ++i) fprintf(output, " ")

    int printedCount = fprintf(output, "PROFILER: %d events", event_count);
    int printedCount2 = 0;
    if (events_to_skip == 0)
    {
        printedCount2 = fprintf(output, " | untimed since last full profile : %s", timeToStr(untimed_range, buffer));
        if (printedCount2 < 0)
            printedCount2 = 0;
    }
    printedCount += printedCount2;
    if (printedCount < 0) printedCount = 0;
    PADDING_STR(printedCount, fileMaxLen + 1 + lineMaxLen + 4 + nameMaxLen + 2);
    fprintf(output, "     Internal time   ");
    fprintf(output, "     Total time      ");
    fprintf(output, "     Internal time/hit  hit count");
    fprintf(output, "\n");

    for (int h = 0; h < count; ++h)
    {
        CycleCount* ccount = ccounts[h];
        u64 internalCount = ccount->time - ccount->timeOfChildren;
        int fileLen = (int)strlen(ccount->file);
        int lineLen = countNbChars(ccount->line);
        int nameLen = (int)strlen(ccount->name);
        PADDING_STR(fileLen, fileMaxLen);
        fprintf(output, "%.*s(%d)", fileLen, ccount->file, ccount->line);
        PADDING_STR(lineLen, lineMaxLen);
        fprintf(output, " : ");
        PADDING_STR(nameLen, nameMaxLen);
#undef PADDING_STR
        fprintf(output, "%.*s  %s (%6.2f%%)  ", nameLen, ccount->name, timeToStr(internalCount, buffer), (float)internalCount / totalTime * 100);
        fprintf(output, "%s (%6.2f%%)  ", timeToStr(ccount->time, buffer), (float)ccount->time / totalTime * 100);
        fprintf(output, "%s/hit (%6.2f%%) [%d]  ", timeToStr(internalCount / ccount->hitCount, buffer), (float)(internalCount / ccount->hitCount) / totalTime * 100, ccount->hitCount);
        fprintf(output, "\n");
    }
    fflush(output);
}


void _printMemUsage(MemoryArena& arena, char* name, FILE* output)
{
    char memBuff[256];
    fprintf(output, "memusage (%s): %s\n", name, memToStr(getMemoryUsage(arena), memBuff));
}


#else

#define getTickCount(...) 0

#define TIMED_BLOCK_BEGIN(...)
#define TIMED_BLOCK_END(...)
#define printMemUsage(...)
#define _printMemUsage(...)
#define printProfile(...)
#define TIMED_BLOCK(...)
#define TIMED_FUNCTION(...)
#define TIMED_BLOCK_WITH_PROFILE(...)
#define TIMED_FUNCTION_WITH_PROFILE(...)
#define MUTE_PROFILE(...)
#define resetProfileState(...)

#endif


