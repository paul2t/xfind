
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
		maxTransitions += uniqueCharsSize;
	}
	uniqueCharsCountUpToHere[s.size] = uniqueCharsCountUpToHere[s.size - 1];
	maxTransitions += uniqueCharsSize;

	char* transitionsChars = pushArray(arena, char, maxTransitions, pushpNoClear());
	SearchState** transitions = pushArray(arena, SearchState*, maxTransitions, pushpNoClear());
	i32 transitionsSize = 0;
	for (i32 i = 0; i <= s.size; ++i)
	{
		b32 isFinal = (i == s.size);

		SearchState* state = states + i;
		state->c = transitionsChars + transitionsSize;
		state->t = transitions + transitionsSize;
		state->final = isFinal;

		if (i > 0) states[i - 1].t[0] = state;

		if (!isFinal)
		{
			addStateTransition(state, caseInsensitive ? char_to_lower(s.str[i]) : s.str[i]);
			++transitionsSize;
		}

		// For each character used up to there, we look at the previous states
		// to see if adding this character would match the corresponding string.
		for (i32 j = 0; j < uniqueCharsCountUpToHere[i]; ++j)
		{
			char cj = uniqueChars[j];
			if (i < s.size && cj == s.str[i]) continue;

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


#if APP_INTERNAL
// Example search:
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
		for (i32 i = 0; i < cn + colIndex-1; ++i) printf(" ");
		for (i32 i = 0; i < res.match.size; ++i) printf("^");
		printf("\n");

		++count;
	}
	printf("%.*s : ", strexp(s));
	if (count == 0)
		printf("no match.\n");
	else if (count == 1)
		printf("%d match.\n", count);
	else
		printf("%d matches.\n", count);
	printf("=======================================\n");
}

void testSearch()
{
	_set_printf_count_output(true);
	MemoryArena arena = {};
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
}
#endif
