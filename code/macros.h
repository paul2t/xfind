
#ifndef Macros_H
#define Macros_H

#include <stdint.h>
#include <cassert>

#define internal_function static
#define local_persist static

typedef int8_t i8;
typedef int8_t i08;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint8_t u08;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef bool b8;
typedef bool b08;
typedef u32 b32;

typedef float f32;
typedef double f64;

typedef size_t memid;
typedef uintptr_t umm;
typedef intptr_t smm;

// Counts the number of elements in an array
#define ArrayCount(tab)		(sizeof(tab) / sizeof(*(tab)))

// Defines an NULL object of the given type
#define TypeOf(type)		((type*)0)

#define AlignDown(val, align) ((val) & ~(((umm)align)-1))
#define AlignUp(val, align)   (((val) + ((umm)align) - 1) & ~(((umm)align)-1))

#define Align4Down(val) AlignDown(val, 4)
#define Align8Down(val) AlignDown(val, 8)
#define Align16Down(val) AlignDown(val, 16)
#define Align32Down(val) AlignDown(val, 32)

#define Align4Up(val) AlignUp(val, 4)
#define Align8Up(val) AlignUp(val, 8)
#define Align16Up(val) AlignUp(val, 16)
#define Align32Up(val) AlignUp(val, 32)

inline void ZeroSize(void* ptr, memid size)
{
    u8* at = (u8*)ptr;
    for (memid i = 0; i < size; ++i) *at++ = 0;
}
// Set all the values of a struct to 0
#define ZeroStruct(var) ZeroSize(&(var), sizeof(var))


#define KiloBytes(num)  ((num)*1024LL)
#define MegaBytes(num)  (KiloBytes(num)*1024LL)
#define GigaBytes(num)  (MegaBytes(num)*1024LL)
#define TeraBytes(num)  (GigaBytes(num)*1024LL)






template <typename F>
struct _ScopeExit
{
	_ScopeExit(F f) : f(f) {}
	~_ScopeExit() { f(); }
	F f;
};

template <typename F>
_ScopeExit<F> MakeScopeExit(F f)
{
	return _ScopeExit<F>(f);
};


#define ScopeExitVarName(...) CONCAT(scope_exit_, CONCAT(__LINE__, CONCAT(_, __COUNTER__)))
#define NamedName(ptr) CONCAT(_named_, ptr)

// Allows to execute code at the end of the current scope.
//
// Examples:
//
// FILE* f = fopen("test.txt", "w");
// ScopeExit(if (f) { fclose(f); f = NULL; });
// == ScopeFClose(f);
//
// int* array = new int[16];
// ScopeExit(delete[] array);
// == ScopeDeleteArray(array);
//
// CATBaseUnknown* unk = ...;
// ScopeExit(CATSysReleasePtr(unk));
// == ScopeRelease(unk);
#define ScopeExit(code) auto ScopeExitVarName() = MakeScopeExit([&]() mutable {code;})
#define ScopeExitByValue(code) auto ScopeExitVarName() = MakeScopeExit([=]() {code;})
#define ScopeExitCapture(capture, code) auto ScopeExitVarName() = MakeScopeExit(capture () {code;})

#define _ScopeExitCall(ptr, function) ScopeExit(if (ptr) { function(ptr); (ptr) = 0; })
#define _ScopeExitCallVar(var, function) ScopeExit(function(&(var)))
#define _ScopeExitCallNamed(ptr, function) auto** NamedName(ptr) = &ptr; ScopeExit(if ((NamedName(ptr)) && *(NamedName(ptr))) { function(*(NamedName(ptr))); *(NamedName(ptr)) = 0; })
#define _ScopeExitCallUnNamed(ptr, function) NamedName(ptr) = NULL
#define _ScopeExitCallValue(ptr, function) ScopeExitByValue(auto* pointer = (ptr); if (pointer) { function(pointer); })

#define _DO_Release(ptr) (ptr)->Release()
#define ScopeRelease(ptr) _ScopeExitCall(ptr, _DO_Release)
#define ScopeReleaseVar(var) _ScopeExitCallVar(var, _DO_Release)
#define ScopeReleaseNamed(ptr) _ScopeExitCallNamed(ptr, _DO_Release)
#define ScopeUnReleaseNamed(ptr) _ScopeExitCallUnNamed(ptr, _DO_Release)
#define ScopeReleaseValue(ptr) _ScopeExitCallValue(ptr, _DO_Release)

#define _DO_Destroy(ptr) (ptr)->Destroy()
#define ScopeDestroy(ptr) _ScopeExitCall(ptr, _DO_Destroy)
#define ScopeDestroyVar(var) _ScopeExitCallVar(var, _DO_Destroy)
#define ScopeDestroyNamed(ptr) _ScopeExitCallNamed(ptr, _DO_Destroy)
#define ScopeUnDestroyNamed(ptr) _ScopeExitCallUnNamed(ptr, _DO_Destroy)
#define ScopeDestroyValue(ptr) _ScopeExitCallValue(ptr, _DO_Destroy)

#define ScopeDelete(ptr) _ScopeExitCall(ptr, delete)
#define ScopeDeleteNamed(ptr) _ScopeExitCallNamed(ptr, delete)
#define ScopeUnDeleteNamed(ptr) _ScopeExitCallUnNamed(ptr, delete)
#define ScopeDeleteValue(ptr) _ScopeExitCallValue(ptr, delete)

#define ScopeDeleteArray(ptr) _ScopeExitCall(ptr, delete[])
#define ScopeDeleteArrayNamed(ptr) _ScopeExitCallNamed(ptr, delete[])
#define ScopeUnDeleteArrayNamed(ptr) _ScopeExitCallUnNamed(ptr, delete[])
#define ScopeDeleteArrayValue(ptr) _ScopeExitCallValue(ptr, delete[])

#define ScopeFree(ptr) _ScopeExitCall(ptr, free)
#define ScopeFreeNamed(ptr) _ScopeExitCallNamed(ptr, free)
#define ScopeUnFreeNamed(ptr) _ScopeExitCallUnNamed(ptr, free)
#define ScopeFreeValue(ptr) _ScopeExitCallValue(ptr, free)


#define ScopeFClose(f) ScopeExit(if (f) { fclose(f); (f) = NULL; })
#define ScopeCATFClose(file) ScopeExit(CATFClose(file); file = 0);

#define SCOPE_LOCALE(locale, value) char* curLocalSettings = strdup(setlocale(locale, (const char *)NULL)); \
									setlocale(locale, value); \
									ScopeExit(if (curLocalSettings) { setlocale(locale, curLocalSettings); free(curLocalSettings); curLocalSettings = 0; });

#define SCOPE_LC_NUMERIC_C() SCOPE_LOCALE(LC_NUMERIC, "C")




// NOTE(xf4): Tools for flags

inline u32 flagSet(u32 flags, u32 flagToSet)
{
    u32 result = flags | flagToSet;
    return result;
}
inline u32 flagUnset(u32 flags, u32 flagToRemove)
{
    u32 result = flags & ~flagToRemove;
    return result;
}
inline b32 flagIsSet(u32 flags, u32 flagToCheck)
{
    b32 result = (flags & flagToCheck);
    return result;
}





//
// NOTE(xf4): Memory Management System.
//

#if 0
void usage()
{
    MemoryArena arena = {}; // Initialization of the arena
    ScopeReleasePtr(&arena); // Clear the memory when leaving the function
    
    // This is just a stack of memory that grow by chunks of 4 megabytes. If you push more than 4 megabytes it will also work.
    // When you push memory on the stack, it can only grow.
    // Use TempMemory if you need to pop memory from the stack.
    
    // By default the memory return is cleared to 0 and aligned to 8 bytes.
    // Aligned to 8 bytes means that the memory address returned by push is a multiple of 8.
    // WARNING: Alignement can only be a power of 2 ! You have been warned ! Don't complain if you shoot yourself in the foot.
    
    // Allocation uses virtual memory, which means, it is only put into physical memory when it is actually used.
    // Therefore if you ask for 10 gigabytes of virtual memory, only the part that you actually push will be considered used.
    // Virtual Memory Space can go up to 16 terabytes.
    // Also, Virtual Memory can only be allocated by pages of 4096 bytes, if you don't use a multiple of 4096, you are just wasting space.
    
    
    int* array1 = pushArray(arena, int, 1024); // This is an array of 1024 int cleared to 0.
    int* array2 = pushArray(arena, int, 1024, pushpNoClear()); // This is an array of 1024 int not cleared.
    int* array3 = pushArray(arena, int, 1024, pushpAlign(16)); // This is an array of 1024 int cleared to 0, aligned by 16 bytes.
    int* array4 = pushArray(arena, int, 1024, pushpAlignNoClear(16)); // This is an array of 1024 int not cleared, aligned by 16 bytes.
    
    int* preserved[4];
    int* lost[4];
    
    for (int i = 0; i < 4; ++i)
    {
        preserved[i] = pushArray(arena, int, 4096);
        
        TempMemory tmp(arena);
        
        lost[i] = pushArray(arena, int, 2048);
        int* integer = pushStruct(arena, int); // This is one integer set to 0.
        int* integer = pushArray(arena, int, 1); // This exactly the same code generated as the previous line.
        float* fp = pushStruct(arena, float, pushpNoClear()); // This is one float not cleared.
        
        // At the end of the scope, the arena is reset to the state it was when the corresponding begin was called.
        // This means that the next push will start overriding memory that was pushed between the begin/end calls.
        // You could also use tmp.clear()
    }
    
    // At this point in the program, "preserved" can still be used. while "lost" should not, because it is already overriden by "preserved".
    
    int* array10 = pushCopy(arena, int, 1024, array3); // Copy array3 into array10
    int* array12 = pushArray(arena, int, 4096); // (size_t)array10 < (size_t)array12 < (size_t)array5
    
    clearMemory(arena); // After this, every push made on arena is now invalid.
    // Everything is popped (including the subarenas, so arenaForSomeWork should not be used either).
    
    int* array6 = pushArray(arena, float, 2048);
    float* farray1 = pushArray(arena, int, 4096);
    
    arena.Release(); // Same as : clearMemory(arena);
}
#endif

inline void* memAlloc(memid size)
{
    void* result = VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return result;
}

inline void memFree(void* memory)
{
    VirtualFree(memory, 0, MEM_RELEASE);
}


void clearMemory(struct MemoryArena& arena);

// Should be a multiple of 16
struct MemoryBlock
{
    umm size;
    u8* base;
    umm used;
    MemoryBlock* prev;
};

struct MemoryArena
{
    MemoryBlock* block;
    umm minimumBlockSize;
    i32 tempCount;
    
    inline void Release() { clearMemory(*this); }
};

inline umm getAlignmentOffset(MemoryArena& arena, umm alignment)
{
    umm alignmentOffset = 0;
    umm resultPointer = (umm)arena.block->base + arena.block->used;
    umm alignMask = alignment - 1;
    if (resultPointer & alignMask)
    {
        alignmentOffset = alignment - (resultPointer & alignMask);
    }
    return alignmentOffset;
}

inline void checkArena(MemoryArena& arena)
{
	assert(arena.tempCount == 0);
}



enum PushParamFlags
{
    PushParamFlags_ClearToZero = (1 << 0),
};

struct PushParam
{
    u16 align;
    u16 paddingAlign;
    u32 flags;
};

inline PushParam pushpDefault()
{
    PushParam result = {};
    result.align = 8;
    result.paddingAlign = 1;
    result.flags = flagSet(result.flags, PushParamFlags_ClearToZero);
    return result;
}

inline PushParam pushpNoClear()
{
    PushParam result = pushpDefault();
    result.flags = flagUnset(result.flags, PushParamFlags_ClearToZero);
    return result;
}

// NOTE(xf4): Both alignments must be powers of 2 greater than 0.
// Can be 1, 2, 4, 8, 16, ...
inline PushParam pushpAlign(u16 align)
{
    PushParam result = pushpDefault();
    result.align = align;
    return result;
}

inline PushParam pushpAlignPadding(u16 align, u16 paddingAlign)
{
    PushParam result = pushpAlign(align);
    result.paddingAlign = paddingAlign;
    return result;
}

inline PushParam pushpAlignNoClear(u16 align)
{
    PushParam result = pushpNoClear();
    result.align = align;
    return result;
}

inline PushParam pushpAlignPaddingNoClear(u16 align, u16 paddingAlign)
{
    PushParam result = pushpAlignNoClear(align);
    result.paddingAlign = paddingAlign;
    return result;
}


#define pushStruct(arena, type, ...) (type*)pushSize_(arena, sizeof(type), __VA_ARGS__)
#define pushArray(arena, type, size, ...) (type*)pushSize_(arena, (size) * sizeof(type), __VA_ARGS__)
#define pushCopy(arena, type, size, source, ...) (type*)pushCopy_(arena, (size) * sizeof(type), source, __VA_ARGS__)
#define pushSize(arena, size, ...) (u8*)pushSize_(arena, size, __VA_ARGS__)

internal_function void* pushSize_(MemoryArena& arena, memid size, PushParam param = pushpDefault())
{
    u8* result = 0;
    umm paddedSize = AlignUp(size, param.paddingAlign);
    
    umm usage = 0;
    umm alignOffset = 0;
    if (arena.block)
    {
        alignOffset = getAlignmentOffset(arena, param.align);
        usage = paddedSize + alignOffset;
    }
    
    if (!arena.block || (arena.block->used + usage > arena.block->size))
    {
        if (!arena.minimumBlockSize)
        {
            arena.minimumBlockSize = MegaBytes(4);
        }
        
        umm blockSize = arena.minimumBlockSize;
        if (blockSize < paddedSize)
            blockSize = paddedSize;
        
        umm allocSize = AlignUp(sizeof(MemoryBlock), param.align) + blockSize;
        MemoryBlock* block = (MemoryBlock*)memAlloc(allocSize);
		assert(block);
        if (!block) return 0;
        
        block->size = allocSize - sizeof(MemoryBlock);
        block->base = (u8*)(block + 1);
        block->used = 0;
        
        block->prev = arena.block;
        arena.block = block;
        
        alignOffset = getAlignmentOffset(arena, param.align);
        usage = paddedSize + alignOffset;
        if (!(arena.block->used + usage <= arena.block->size)) return 0;
    }
    
    result = arena.block->base + arena.block->used + alignOffset;
    arena.block->used += usage;
    
    if (flagIsSet(param.flags, PushParamFlags_ClearToZero))
    {
        ZeroSize(result, paddedSize);
    }
    
    return result;
}

internal_function void* pushCopy_(MemoryArena& arena, memid size, void* source, PushParam param = pushpDefault())
{
    param.flags = flagUnset(param.flags, PushParamFlags_ClearToZero);
    u8* result = pushSize(arena, size, param);
    
    u8* src = (u8*)source;
    u8* dst = result;
    
    for (memid index = 0; index < size; ++index)
    {
        *dst++ = *src++;
    }
    
    return result;
}

inline void freeLastBlock(MemoryArena& arena)
{
    MemoryBlock* block = arena.block;
    arena.block = arena.block->prev;
    memFree(block);
}

inline void clearMemory(MemoryArena& arena)
{
    checkArena(arena);
    while (arena.block)
        freeLastBlock(arena);
}

internal_function umm getMemoryUsage(MemoryArena& arena)
{
    umm result = 0;
    MemoryBlock* block = arena.block;
    while (block)
    {
        result += block->used;
        block = block->prev;
    }
    return result;
}


struct TempMemory
{
    inline TempMemory(MemoryArena& arena) : arena(&arena), block(arena.block)
    {
        if (block)
        {
            this->used = block->used;
        }
        ++arena.tempCount;
    }
    
    inline ~TempMemory()
    {
        clear();
    }
    
    inline void commit()
    {
		assert(arena);
		if (!arena) return;
        --arena->tempCount;
		assert(arena->tempCount >= 0);
        arena = 0;
    }
    
    inline void clear()
    {
        if (arena)
        {
            while (arena->block != this->block)
                freeLastBlock(*arena);
            
            if (arena->block)
            {
				assert(arena->block->used >= this->used);
                arena->block->used = this->used;
            }
            
            --arena->tempCount;
            assert(arena->tempCount >= 0);
            arena = 0;
        }
    }
    
    
    MemoryArena* arena;
    MemoryBlock* block;
    umm used;
};


#endif
