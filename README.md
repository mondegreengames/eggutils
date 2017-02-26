# eggutils
Utils for working with egg archives


## EggArchiveBuilder
EggArchiveBuilder is a command-line tool that lets you do things like create egg archives, 
list the files that are inside, and extract files from inside the egg file.


## Egg file format

First comes the header:

* char[4] - magic - Appears as "EGGA" in the file
* uint16 - version number - should be 1
* uint16 - flags - unused right now
* uint64 - time the egg was built - This is actually a Win32 FILETIME struct
* uint32 - total number of files within the egg
* uint32 - Offset to the filenames (relative to start of the file)
* uint32 - Offset to the TOC (relative to the start of the file)
* uint32 - unused

Next comes the actual file contents, just one after another. Note that the file might be compressed. Also, our tool makes sure that each file begins on an 8-byte boundary.

Once all the files are written, the filenames or TOC will appear (the order of which appears first doesn't really matter). The filenames are written in case-insensitive alphabetical order. Plus, the filenames and the files in the TOC are written in the same order. So the nth file in the list of filenames is the nth file in the TOC. Remember that the file contents might be (and probably will be) written in a different order from the filenames.

The filenames:

* uint8 - the length of the filename, not including the 0 at the end (obviously filenames can't be longer than 255 characters)
* char[?] - the filename, terminated with a 0

The TOC (table of contents) (btw, the TOC should be written at an 8-byte boundary):

* uint32 - Offset to the file contents (relative to the start of the file)
* uint32 - Compressed size of the file (will be the same as the uncompressed size if the file isn't compressed)
* uint32 - Uncompressed size of the file
* uint32 - flags (see note below)

Right now the only flag is 0x1, which means the file is compressed with LZ4 compression. 0x0 means uncompressed.

## FAQ
### What is an "egg archive?"
They're basically like zip files, but the format is a bit simpler. 
Our game Chickens n Kittens uses egg files to store all its data files.

### Why didn't you just use zip files in Chickens n Kittens?
We could have. Maybe we even should have. But we didn't.

### Why not just store all the individual files you need in a folder instead of packing them in egg/zip/whatever archives?
We could have, but in our tests getting rid of the overhead of opening each
file individually made a very noticable difference in terms of load times.
