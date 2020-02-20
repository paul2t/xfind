
struct SearchState
{
	char* c = 0;
	SearchState** t = 0;
	i32 n = 0;
	b32 final = false;
};

struct SearchStateMachine
{
	i32 nbStates = 0;
	SearchState* states = 0;
	char* transitionsChars = 0;
	SearchState** transitions = 0;
	b32 caseInsensitive = false;
};


inline b32 hasCharUpTo(char* str, i32 count, char c, b32 caseInsensitive = false)
{
	for (i32 i = 0; i < count; ++i)
	{
		if ((caseInsensitive && char_to_lower(str[i]) == c) || (!caseInsensitive && str[i] == c))
			return true;
	}
	return false;
}

void addStateTransition(SearchState* in, char c, SearchState* out = NULL)
{
	int ni = in->n++;
	in->c[ni] = c;
	in->t[ni] = out;
}

SearchStateMachine buildSearchStateMachine(MemoryArena& arena, String s, b32 caseInsensitive = false)
{
	SearchStateMachine result = {};
	if (s.size <= 0) return result;

	SearchState* states = pushArray(arena, SearchState, s.size + 1, pushpNoClear());
	i32 uniqueCharsSize = 0;
	char* uniqueChars = new char[s.size];
	i32* uniqueCharsCountUpToHere = new i32[s.size + 1];

	i32 maxTransitions = 0;
	for (i32 i = 0; i < s.size; ++i)
	{
		char c = caseInsensitive ? char_to_lower(s.str[i]) : s.str[i];
		if (!hasCharUpTo(uniqueChars, uniqueCharsSize, c))
			uniqueChars[uniqueCharsSize++] = c;
		uniqueCharsCountUpToHere[i] = uniqueCharsSize;
		maxTransitions += uniqueCharsSize*2;
	}
	uniqueCharsCountUpToHere[s.size] = uniqueCharsCountUpToHere[s.size - 1];
	maxTransitions += uniqueCharsSize*2;

	char* transitionsChars = pushArray(arena, char, maxTransitions, pushpNoClear());
	SearchState** transitions = pushArray(arena, SearchState*, maxTransitions, pushpNoClear());
	i32 transitionsSize = 0;
	for (i32 i = 0; i <= s.size; ++i)
	{
		b32 isFinal = (i == s.size);
		char c = s.str[i];

		SearchState* state = states + i;
		state->c = transitionsChars + transitionsSize;
		state->t = transitions + transitionsSize;
		state->final = isFinal;

		if (i > 0)
		{
			SearchState* lastState = states + (i - 1);
			for (int j = 0; j < lastState->n; ++j)
			{
				if (!lastState->t[j])
					lastState->t[j] = state;
			}
		}

		char c2 = 0;
		if (!isFinal)
		{
			addStateTransition(state, caseInsensitive ? char_to_lower(c) : c);
			++transitionsSize;

			// TODO search path assimilating '/' with '\'
#if 0
			if (c == '\\') c2 = '/';
			else if (c == '/') c2 = '\\';

			if (c2)
			{
				addStateTransition(state, c2);
				++transitionsSize;
			}
#endif
		}

		// For each character used up to there, we look at the previous states
		// to see if adding this character would match the corresponding string.
		for (i32 j = 0; j < uniqueCharsCountUpToHere[i]; ++j)
		{
			char cj = uniqueChars[j];
			if (i < s.size && (cj == c || cj == c2)) continue;

			// For each previous state (including the current one)
			for (i32 k = i; k > 0; --k)
			{
				char ck = (caseInsensitive ? char_to_lower(s.str[k - 1]) : s.str[k - 1]);
				if (cj == ck && match_insensitive(make_string(s.str, k - 1), make_string(s.str + i + 1 - k, k - 1)))
				{
					addStateTransition(state, cj, states + k);
					++transitionsSize;
					break;
				}
			}
		}
	}
	assert(transitionsSize <= maxTransitions);
	delete[] uniqueChars;
	delete[] uniqueCharsCountUpToHere;

	result.caseInsensitive = caseInsensitive;
	result.nbStates = s.size + 1;
	result.states = states;
	result.transitionsChars = transitionsChars;
	result.transitions = transitions;
	return result;
}

struct SearchResult
{
	String match = {};
	SearchState* state = 0;
	char* linestart = 0;
	i32 lineIndex = 1;
};

SearchResult searchPattern(String s, SearchStateMachine pattern, SearchResult lastResult = {})
{
	SearchResult result = {};
	SearchState* root = pattern.states;
	SearchState* start = root;
	i32 lineIndex = 1;
	String search = s;
	char* linestart = s.str;

	if (lastResult.match.str)
	{
		start = lastResult.state;
		String lm = lastResult.match;
		i32 diff = (i32)(lm.str - search.str);
		search.size = search.size - diff - lm.size;
		search.memory_size = search.memory_size - diff - lm.size;
		if (search.memory_size < 0) search.memory_size = 0;
		search.str = lm.str + lm.size;
		lineIndex = lastResult.lineIndex;
		linestart = lastResult.linestart;
	}

	if (!root) return result;

	SearchState* state = start;
	for (int i = 0; i < search.size; ++i)
	{
		char c = pattern.caseInsensitive ? char_to_lower(search.str[i]) : search.str[i];
		int nbOuts = state->n;
		char* cOuts = state->c;
		SearchState** sOuts = state->t;
		bool found = false;
		for (int oi = 0; oi < nbOuts; ++oi)
		{
			if (c == cOuts[oi])
			{
				state = sOuts[oi];
				found = true;
				if (state->final)
				{
					result.match = substr(search, i + 1 - pattern.nbStates + 1, pattern.nbStates - 1);
					result.linestart = linestart;
					result.lineIndex = lineIndex;
					result.state = state;
					return result;
				}
				break;
			}
		}
		if (!found)
			state = root;

		if (c == '\n')
		{
			++lineIndex;
			linestart = search.str + i + 1;
		}
	}

	result.state = state;
	result.match.str = search.str + search.size;
	result.match.size = 0;
	result.linestart = result.match.str;
	result.lineIndex = lineIndex;

	return result;
}



#if 0

#include <intrin.h>
struct WideSearchState
{
	__m128* masks = 0;
	__m128* chars = 0;
	WideSearchState** trans = 0;
	i32 n = 0;
	b32 final = false;
};

struct WideSearchStateMachine
{
	String s = {};
	__m128i mask = {};
	__m128i chars = {};
	__m128i first = {};
	i32 nbStates = 0;
	WideSearchState* states = 0;
	char* transitionsChars = 0;
	WideSearchState** transitions = 0;
	b32 caseInsensitive = false;
};

WideSearchStateMachine buildWideSearchStateMachine(MemoryArena& arena, String s, b32 caseInsensitive = false)
{
	WideSearchStateMachine result = {};
	if (s.size <= 0) return result;
	
	result.s = s;
	u8 chars[16] = {};
	u8 mask[16] = {};
	for (int i = 0; i < 16 && i < s.size; ++i)
	{
		chars[i] = s.str[i];
		mask[i] = 0xff;
	}
	result.first = _mm_set1_epi8(chars[0]);
	result.chars = _mm_set_epi8(chars[15], chars[14], chars[13], chars[12], chars[11], chars[10], chars[9], chars[8], chars[7], chars[6], chars[5], chars[4], chars[3], chars[2], chars[1], chars[0]);
	result.mask = _mm_set_epi8(mask[15], mask[14], mask[13], mask[12], mask[11], mask[10], mask[9], mask[8], mask[7], mask[6], mask[5], mask[4], mask[3], mask[2], mask[1], mask[0]);

	return result;
}

inline i32 getLeastSignificantBit(u64 value)
{
	i32 result;
	if (!_BitScanForward64((DWORD*)&result, value))
		result = 64;
	return result;
}

inline i32 getLeastSignificantBit(u32 value)
{
	i32 result;
	if (!_BitScanForward((DWORD*)&result, value))
		result = 32;
	return result;
}

inline i32 getMostSignificantBit(u32 value)
{
	i32 result;
	if (!_BitScanReverse((DWORD*)&result, value))
		result = -1;
	return result;
}

inline i32 nextLSB(u32 value, i32 lastIndex = -1)
{
	u32 shifted = value >> (lastIndex + 1);
	i32 scan = getLeastSignificantBit(shifted);
	i32 result = scan + lastIndex + 1;
	return result;
}

inline u64 clearLeastSignificantBit(u64 value)
{
	return value & (value - 1);
}

inline u32 clearLeastSignificantBit(u32 value)
{
	return value & (value - 1);
}

// From: http://0x80.pl/articles/simd-strfind.html
i32 avx2_strstr_anysize(const char* s, i32 n, const char* needle, i32 k) {

	const __m256i first = _mm256_set1_epi8(needle[0]);
	const __m256i last = _mm256_set1_epi8(needle[k - 1]);

	for (i32 i = 0; i < n; i += 32) {

		const __m256i block_first = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i));
		const __m256i block_last = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i + k - 1));

		const __m256i eq_first = _mm256_cmpeq_epi8(first, block_first);
		const __m256i eq_last = _mm256_cmpeq_epi8(last, block_last);

		uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

		while (mask != 0) {

			const auto bitpos = getLeastSignificantBit(mask);

			if (memcmp(s + i + bitpos + 1, needle + 1, k - 2) == 0) {
				return i + bitpos;
			}

			mask = clearLeastSignificantBit(mask);
		}
	}

	return -1;
}

i32 sse42_strstr_anysize(const char* s, i32 n, const char* needle, i32 k)
{
	assert(k > 0);
	assert(n > 0);

	const __m128i N = _mm_loadu_si128((__m128i*)needle);

	for (i32 i = 0; i < n; i += 16)
	{
		const int mode = _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ORDERED | _SIDD_BIT_MASK;

		const __m128i D = _mm_loadu_si128((__m128i*)(s + i));
		const __m128i res = _mm_cmpestrm(N, k, D, n - i, mode);
		uint64_t mask = _mm_cvtsi128_si64(res);

		while (mask != 0)
		{
			const auto bitpos = getLeastSignificantBit(mask);

			// we know that at least the first character of needle matches
			if (memcmp(s + i + bitpos + 1, needle + 1, k - 1) == 0) {
				return i + bitpos;
			}

			mask = clearLeastSignificantBit(mask);
		}
	}

	return -1;
}

struct WideSearchResult
{
	String match = {};
	WideSearchState* state = 0;
	char* linestart = 0;
	i32 lineIndex = 1;
	i32 bytesLeftInRegister = 0;
};

WideSearchResult searchPattern(String s, WideSearchStateMachine pattern, WideSearchResult lastResult = {})
{
	WideSearchResult result = {};
	//i32 pos = avx2_strstr_anysize(s.str, s.size, pattern.s.str, pattern.s.size);
	//return result;


	i32 plen = pattern.s.size;
	if (plen > 16) plen = 16;

	__m128i cmp = pattern.first;
	__m128i wstr = pattern.chars;
	for (i32 si = 0; si < (s.size/16); ++si)
	{
		// Load the string into a register
		__m128i swide = _mm_loadu_si128(((__m128i*)s.str) + si);

		// Look if the first letter of the search pattern is present in the result.
		__m128i scomp = _mm_cmpeq_epi8(cmp, swide);

		// Get the mask of the bytes where the first letter matches.
		u32 firstLetterMask = _mm_movemask_epi8(scomp);
		if (firstLetterMask)
		{
			if (pattern.s.size > 1)
			{
				i32 lastMatchIndex = getMostSignificantBit(firstLetterMask);
				// Shift the matches of the first letter by one to the left. To get the positions for the second letter.
				__m128i secondLetterWMask = _mm_slli_si128(scomp, 1);

				// Make a buffer filled with the second letter
				__m128i secondLetter = _mm_set1_epi8(pattern.s.str[1]);
				i32 secondLetterMask = _mm_movemask_epi8(secondLetter);
				if (secondLetterMask)
				{
					// second letter is only where it could be.
					__m128i secondLetterPos = _mm_and_si128(secondLetter, secondLetterWMask);
					u32 firstLetterMask = _mm_movemask_epi8(scomp);
					i32 last2LMatch = getMostSignificantBit()
				}


				u32 found = _mm_cmpestri(swide, 16, wstr, plen, _SIDD_CMP_EQUAL_ORDERED);
				i32 findex = getLeastSignificantBit(found);

			}
#if 0
			for (i32 index = nextLSB(firstLetterMask); index < 16; index = nextLSB(firstLetterMask, index))
			{
				assert(s.str[si * 16 + index] == pattern.s.str[0]);
			}
#endif
		}

		int breakhere = 1;
	}

	return result;
}



#if APP_INTERNAL
// Example search:

#define USE_WIDE_SEARCH 1
#if USE_WIDE_SEARCH
#define SearchStateMachine WideSearchStateMachine
#define SearchResult WideSearchResult
#define buildSearchStateMachine buildWideSearchStateMachine
#endif

void searchAll(String s, SearchStateMachine ssm)
{
	i32 count = 0;
	for (SearchResult res = searchPattern(s, ssm); res.match.size; res = searchPattern(s, ssm, res))
	{
		i32 colIndex = (i32)(res.match.str - res.linestart) + 1;
		String line = {};
		line.str = res.linestart;
		line.size = colIndex + res.match.size;
		while (line.str[line.size] && line.str[line.size] != '\n' && line.str[line.size] != '\r') ++line.size;

		i32 cn = 0;
		printf("Found pattern at line %d column %d : \"%n%.*s\"\n", res.lineIndex, colIndex, &cn, strexp(line));
		for (i32 i = 0; i < cn + colIndex - 1; ++i) printf(" ");
		for (i32 i = 0; i < res.match.size; ++i) printf("^");
		printf("\n");

		++count;
	}
	//printf("%.*s : ", strexp(s));
	if (count == 0)
		printf("no match.\n");
	else if (count == 1)
		printf("%d match.\n", count);
	else
		printf("%d matches.\n", count);
	//printf("=======================================\n");
}

void testSearch()
{
	_set_printf_count_output(true);
	MemoryArena arena = {};
	String fcoder = readEntireFile(arena, "C:\\work\\xfind\\code\\4coder_string.h");

#if 0
	String pattern = make_lit_string("M");
	printf("Looking for pattern : \"%.*s\"\n", strexp(pattern));
	SearchStateMachine ssm = buildSearchStateMachine(arena, pattern);

	searchAll(make_lit_string("MMOM"), ssm);
	searchAll(make_lit_string("MOMMOMMO"), ssm);
	searchAll(make_lit_string("MOMMOMOMMO"), ssm);
	searchAll(make_lit_string("MOOMMO"), ssm);
	searchAll(make_lit_string("MOMOMMO"), ssm);
	searchAll(make_lit_string("MMOMMOMMO"), ssm);
	searchAll(make_lit_string("MOMMOMMMO"), ssm);
	searchAll(make_lit_string("MMMMMMMMMMMOMMMO"), ssm);
#endif


	SearchStateMachine ssm4coder = buildSearchStateMachine(arena, make_lit_string("4coder"));

	searchAll(fcoder, ssm4coder);


	arena.Release();
}


#if USE_WIDE_SEARCH
//#undef SearchStateMachine WideSearchStateMachine
//#undef SearchResult WideSearchResult
//#undef buildSearchStateMachine buildWideSearchStateMachine
#endif

#endif

#endif
