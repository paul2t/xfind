
internal i32 parseExtensions(String* extensions, i32 extensionsMaxSize, char* input)
{
	return splitByChar(extensions, extensionsMaxSize, input, ';');
}

// tempArena is restored to original memory usage.
internal i32 parsePaths(MemoryArena& tempArena, String* paths, i32 pathsMaxSize, char* input, b32& allPathsExist)
{
	i32 pathsSize = splitByChar(paths, pathsMaxSize, input, ';');

	allPathsExist = pathsSize > 0;
	for (int pi = 0; pi < pathsSize; ++pi)
	{
		String& path = paths[pi];
		if (path.size > 0 && char_is_slash(path.str[path.size - 1]))
			path.size--;
		b32 pathIsDir = PathFileIsDirectoryNZ(tempArena, path);
		allPathsExist = allPathsExist && pathIsDir;
		//printf("search path: %.*s (%s)\n", strexp(path), pathExists ? "valid" : "invalid");
	}

	return pathsSize;
}

