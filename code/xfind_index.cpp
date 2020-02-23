
inline u32 hash(String s)
{
	char* at = s.str;
	u32 result = 0;
	for (i32 i = 0; i < s.size; ++i)
		result = (result << 8 | result >> 24) + *at;
	return result;
}

static FileIndexEntry* getFileFromPath(FileIndex* fileIndex, String path)
{
	i32 pathHash = hash(path) % fileIndex->filePathHashSize;
	FileIndexEntry* entry = fileIndex->filePathHash[pathHash];
	while (entry && !match(entry->path, path)) entry = entry->nextInPathHash;
	return entry;
}


static FileIndex createFileIndex(u32 filePathHashSize = PATH_HASH_SIZE)
{
	FileIndex result = {};
	result.filePathHashSize = filePathHashSize;
	result.filePathHash = (FileIndexEntry**)malloc(filePathHashSize * sizeof(FileIndexEntry*));
	memset(result.filePathHash, 0, result.filePathHashSize * sizeof(FileIndexEntry*));
	return result;
}

static FileIndexEntry* newFileIndexEntry()
{
	FileIndexEntry* entry = new FileIndexEntry;
	*entry = {};
	return entry;
}

static void addFileIndexEntry(FileIndex* fileIndex, FileIndexEntry* entry, i32 pathHash)
{
	AtomicListInsert(fileIndex->filePathHash[pathHash], entry, entry->nextInPathHash);
	AtomicListInsert(fileIndex->firstFile, entry, entry->next);
}

static FileIndexEntry* addFileIndexEntry(FileIndex* fileIndex, String path, i32 pathHash, i32 rootSize = 0, u32 expectedReads = 0)
{
	FileIndexEntry* entry = newFileIndexEntry();

	entry->modifiedSinceLastSearch = true;
	entry->path.str = strdup(path.str);
	entry->path.size = path.size;
	entry->path.memory_size = path.size + 1;
	entry->relpath = substr(entry->path, rootSize);

	ScopeMutexWrite(&fileIndex->mutex, expectedReads);
	addFileIndexEntry(fileIndex, entry, pathHash);

	return entry;
}

static FileIndexEntry* getOrCreateFileFromPath(FileIndex* fileIndex, String path, i32 rootSize = 0, u32 expectedReads = 0)
{
	ScopeMutexRead(&fileIndex->mutex);
	i32 pathHash = hash(path) % fileIndex->filePathHashSize;

	FileIndexEntry* entry = fileIndex->filePathHash[pathHash];
	while (entry && !match(entry->path, path)) entry = entry->nextInPathHash;
	if (!entry)
		entry = addFileIndexEntry(fileIndex, path, pathHash, rootSize, expectedReads+1);

	entry->relpath = substr(entry->path, rootSize);

	return entry;
}

static void freeFileIndexEntry(FileIndexEntry* entry)
{
	free(entry->content.str);
	free(entry->path.str);
	*entry = {};
	delete entry;
}



