# eggutils
Utils for working with egg archives


## EggArchiveBuilder
EggArchiveBuilder is a command-line tool that lets you do things like create egg archives, 
list the files that are inside, and extract files from inside the egg file.

## FAQ
### What is an "egg archive?"
They're basically like zip files, but the format is a bit simpler. 
Our game Chickens n Kittens uses egg files to store all its data files.

### Why didn't you just use zip files in Chickens n Kittens?
We could have. Maybe we even should have. But we didn't.

### Why not just store all the individual files you need in a folder instead of packing them in egg/zip/whatever archives?
We could have, but in our tests getting rid of the overhead of opening each
file individually made a very noticable difference in terms of load times.
