
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




//
// Perfos
//

#if APP_INTERNAL

inline u64 getTickCount()
{
	return GetTickCount64();
}

#define MEMSTR_FORMAT "%.1lf "
char* memstr[5] =
{
    MEMSTR_FORMAT "octets",
    MEMSTR_FORMAT "Ko",
    MEMSTR_FORMAT "Mo",
    MEMSTR_FORMAT "Go",
    MEMSTR_FORMAT "To",
};

internal_function char* memToStr(umm size, char* buffer)
{
    int pwr10 = 0;
    double result = (double)size;
    while (result > 1024.0)
    {
        result /= 1024;
        ++pwr10;
    }
    sprintf(buffer, memstr[pwr10], result);
    return buffer;
}

#define CYSTR_FORMAT "%6.2lf "
char* cystr[7] =
{
    CYSTR_FORMAT " cy",
    CYSTR_FORMAT "Kcy",
    CYSTR_FORMAT "Mcy",
    CYSTR_FORMAT "Gcy",
    CYSTR_FORMAT "Tcy",
    CYSTR_FORMAT "Ecy",
    CYSTR_FORMAT "Pcy",
};

internal_function char* cyToStr(umm size, char* buffer)
{
    int pwr10 = 0;
    double result = (double)size;
    while (result > 1000.0)
    {
        result /= 1000;
        ++pwr10;
    }
    sprintf(buffer, cystr[pwr10], result);
    return buffer;
}

#define printMemUsage(arena, ...) _printMemUsage(arena, #arena, __VA_ARGS__)
internal_function void _printMemUsage(MemoryArena& arena, char* name, FILE* output = stderr)
{
    char memBuff[256];
    fprintf(output, "memusage (%s): %s\n", name, memToStr(getMemoryUsage(arena), memBuff));
}




enum DebugType
{
    DebugType_Unknown,
    
    DebugType_FrameMarker,
    DebugType_BlockBegin,
    DebugType_BlockEnd,
    
    DebugType_NoMoreDebugEvent,
};

struct DebugEvent
{
    u64 clock;
    char* name;
    char* file;
    char* function;
    int line;
    u32 count;
    u8 type;
};

struct DebugState
{
    u32 maxEventCount;
    u32 eventCount;
    DebugEvent* events;
    MemoryArena debugArena;
};

internal_function DebugState initDebugState()
{
    DebugState result = {};
    result.maxEventCount = 512*65536;
    result.events = pushArray(result.debugArena, DebugEvent, result.maxEventCount+1, pushpNoClear());
    return result;
}

static DebugState _debugState = initDebugState();
#define GetClockCycleCount __rdtsc

#define TIMED_BLOCK_BEGIN(name) DEBUG_EVENT(DebugType_BlockBegin, name)
#define TIMED_BLOCK_END(name, ...) DEBUG_EVENT(DebugType_BlockEnd, name, __VA_ARGS__)
#define DEBUG_EVENT(type, name, ...) recordDebugEvent(&_debugState, __FILE__, __FUNCTION__, __LINE__, type, name, __VA_ARGS__)
#define TIMED_BLOCK(name, ...) TimedBlock _timed_block_ ## __COUNTER__(name, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
internal_function void recordDebugEvent(DebugState* debugState, char* file, char* function, int line, DebugType type, char* name, u32 count = 1)
{
    u64 clock = GetClockCycleCount();
    // TODO(xf4): This is not thread safe
    if (debugState->eventCount < debugState->maxEventCount)
    {
        DebugEvent* event = debugState->events + debugState->eventCount;
        debugState->eventCount++;
        event->clock = clock;
        event->name = name;
        event->file = file;
        event->function = function;
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
}

struct TimedBlock
{
    char* name;
    char* file;
    char* function;
    int line;
    int count;
    TimedBlock(char* name, char* file, char* function, int line, int count = 1) : name(name), count(count), file(file), function(function), line(line)
    {
        recordDebugEvent(&_debugState, file, function, line, DebugType_BlockBegin, name);
    }
    ~TimedBlock()
    {
        recordDebugEvent(&_debugState, file, function, line, DebugType_BlockEnd, name, count);
    }
};




struct CycleCount
{
    u32 hitCount;
    u64 cycles;
    u64 cyclesOfChildren;
    char* name;
    char* file;
    char* function;
    int line;
    CycleCount* nextInHash;
};

struct DebugFrame
{
    DebugEvent* start;
    u64 cyclesOfChildren;
};

#define CYCLE_HASH_SIZE 4096
inline u32 getCycleHashValue(DebugEvent event)
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
internal_function void printCycleCounts(DebugState* debugState = &_debugState, FILE* output = stderr)
{
    // NOTE(xf4): Do not use TIMED_BLOCK in this function
    TempMemory temp(debugState->debugArena);
    i32 stackSize = 0;
    DebugFrame stack[1024];
    CycleCount** cycleHashTable = pushArray(debugState->debugArena, CycleCount*, CYCLE_HASH_SIZE);
    DebugFrame* root = stack + stackSize++;
    ZeroStruct(*root);
    u64 totalCycles = 0;
    if (debugState->eventCount)
    {
        DebugEvent first = debugState->events[0];
        DebugEvent last = debugState->events[debugState->eventCount-1];
        totalCycles = last.clock - first.clock;
        root->start = debugState->events;
        root->cyclesOfChildren = 0;
    }
    for (u32 ei = 0; ei < debugState->eventCount; ++ei)
    {
        DebugEvent* event = debugState->events + ei;
        if (event->type == DebugType_BlockBegin)
        {
            DebugFrame* frame = stack + stackSize++;
            frame->start = event;
            frame->cyclesOfChildren = 0;
        }
        else if (event->type == DebugType_BlockEnd)
        {
            DebugFrame frame = stack[--stackSize];
            DebugEvent start = *frame.start;
            if (!match(event->name, start.name))
            {
                fprintf(stderr, "WARNING: Begin/End event names not matching: \"%s\" and \"%s\"\n", start.name, event->name);
            }
            u32 hashvalue = getCycleHashValue(start);
            CycleCount* ccout = cycleHashTable[hashvalue];
            while (ccout)
            {
                if (ccout->line == start.line && match(ccout->file, start.file) && match(ccout->name, start.name))
                    break;
                ccout = ccout->nextInHash;
            }
            if (!ccout)
            {
                ccout = pushStruct(debugState->debugArena, CycleCount);
                ccout->name = start.name;
                ccout->file = start.file;
                ccout->line = start.line;
                ccout->function = start.function;
                ccout->nextInHash = cycleHashTable[hashvalue];
                cycleHashTable[hashvalue] = ccout;
            }
            ccout->hitCount += event->count;
            u64 cycles = (event->clock - start.clock);
            ccout->cycles += cycles;
            ccout->cyclesOfChildren += frame.cyclesOfChildren;
            if (stackSize > 0)
            {
                DebugFrame* parent = stack + stackSize-1;
                parent->cyclesOfChildren += cycles;
            }
        }
        else
        {
            assert(0);
        }
    }
    if (stackSize != 1)
    {
        fprintf(stderr, "WARNING: perfos stacksize = %d\n", stackSize);
        for (int i = stackSize-1; i > 0; --i)
        {
            fprintf(stderr, "stack[%d] = %s\n", i, stack[i].start->name);
        }
    }
    
    int count = 1; // NOTE(xf4): Because we add the root to the list
    for (int h = 0; h < CYCLE_HASH_SIZE; ++h)
    {
        CycleCount* ccount = cycleHashTable[h];
        while (ccount)
        {
            ++count;
            ccount = ccount->nextInHash;
        }
    }
    
    CycleCount** ccounts = pushArray(debugState->debugArena, CycleCount*, count, pushpNoClear());
    int cindex = 0;
    if (root->start)
    {
        CycleCount* ccount = ccounts[cindex++] = pushStruct(debugState->debugArena, CycleCount);
        DebugEvent* event = root->start;
        ccount->name = "#GlobalScope#";
        ccount->file = event->file;
        ccount->line = event->line;
        ccount->function = event->function;
        ccount->cycles = totalCycles;
        ccount->cyclesOfChildren = root->cyclesOfChildren;
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
    
    for (int outer = 0; outer < count-1; ++outer)
    {
        bool sorted = true;
        for (int inner = 0; inner < count-1; ++inner)
        {
            CycleCount* a = ccounts[inner];
            CycleCount* b = ccounts[inner+1];
            if (a->cycles-a->cyclesOfChildren < b->cycles-b->cyclesOfChildren)
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
    fprintf(output, "\nCycle counts: %s\n", cyToStr(totalCycles, buffer));
    //fprintf(output, "Global scope: %s (%.2f%%)\n", cyToStr(totalCycles-root->cyclesOfChildren, buffer), (float)(totalCycles-root->cyclesOfChildren) / totalCycles * 100);
    for (int h = 0; h < count; ++h)
    {
        CycleCount* ccount = ccounts[h];
        u64 internalCount = ccount->cycles - ccount->cyclesOfChildren;
        fprintf(output, "%.50s(%4d): ", ccount->file, ccount->line);
        fprintf(output, "%20s  %s (%6.2f%%)  ", ccount->name, cyToStr(internalCount, buffer), (float)internalCount / totalCycles * 100);
        fprintf(output, "%s (%6.2f%%)  ", cyToStr(ccount->cycles, buffer), (float)ccount->cycles / totalCycles * 100);
        fprintf(output, "%s/hit (%6.2f%%) [%d]  ", cyToStr(internalCount/ccount->hitCount, buffer), (float)(internalCount/ccount->hitCount) / totalCycles * 100, ccount->hitCount);
        fprintf(output, "\n");
    }
}
#else

#define getTickCount(...) 0

#define TIMED_BLOCK_BEGIN(...)
#define TIMED_BLOCK_END(...)
#define printMemUsage(...)
#define printCycleCounts(...)
#define TIMED_BLOCK(...)

#endif


