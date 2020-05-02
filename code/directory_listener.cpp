#include <cstdio>
#include <ctime>

#include "watch_directory.h"
int main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s path1 [path2] [path3] ...", argv[0]);
		return 1;
	}

	char** paths = argv + 1;
	int paths_size = argc - 1;

	WatchDir wd = watchdir_start(paths, paths_size);
	for (;;)
	{
		WatchDirEvent* evt = watchdir_get_event(wd);
		if (!evt) continue;


		if (evt->created)
			printf("+ ");
		else if (evt->deleted)
			printf("- ");
		else if (evt->modified)
			printf("~ ");
		else if (evt->existed && evt->old_name_size)
			printf("  ");
		else
			continue;

		time_t now = time(NULL);
		// printf("%d/%02d/%02d-%02d:%02d:%02d ", t.tm_year, t.tm_mon+1, )
		char       buf[80];
		struct tm ts;
		localtime_s(&ts, &now);
	    strftime(buf, sizeof(buf), "%Y/%m/%d-%H:%M:%S", &ts);
	    printf("%s", buf);

		if (evt->old_name_size)
			printf(" %s -> %s", evt->old_name, evt->name);
		else
			printf(" %s", evt->name);

		printf("\n");
	}

	// Unreachable
	// watchdir_stop(wd);

	// return 1;
}
