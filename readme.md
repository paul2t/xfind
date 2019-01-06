xfind is a tool to search for text in multiple files inside sevaral folders.
Just choose a directory and some file extensions.
When indexing is finished (about 10 seconds for 10.000 files on my SSD)
You can then quickly search for a string in those files.
First it shows the file names matching your string.
Then it instantly shows the matching lines.

[Example search](https://bitbucket.org/Rednaj/xfind/downloads/xfind_sample_4coder.png)

You can select a line with up/down arrows. Also page up/down.
Then press enter to open this file using a specified command line.
In the command line, %p will be replaced by the path of the file. %l will be replaced by the line number. %c will be replaced by the column number.
You can specify as many arguments as you whish.

The maximum file size allowed is 10MB. This is too prevent loading giant files that could be present.
The maximum number of results shown is 100. (Could be easily increased, but would slow down the search)

The indexing only consists of loading all the files into memory.
The search is basic: it will look through all the files to look for the string.

The indexing and search are multithreaded. It uses as many logical cores as your computer has.
Therefore even while it is working (searching / indexing), the UI will be responsive.
There is absolutely no optimization done yet.


Only works on Windows (for now).
