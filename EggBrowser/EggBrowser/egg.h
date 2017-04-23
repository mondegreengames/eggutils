#ifndef MONDEGREENGAMES_EGG_H
#define MONDEGREENGAMES_EGG_H

struct megg_info
{
	unsigned int NumFiles;

// zero-length arrays aren't standard C++, but Visual Studio and G++ both support them.
// Disable the warnings.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4200)
#endif
	struct Filename
	{
		unsigned char Length;
		char Name[0];
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	Filename* Filenames;

	struct TOC
	{
		unsigned int FileContentOffset;
		unsigned int CompressedSize;
		unsigned int UncompressedSize;
		unsigned int Flags;
	};
	TOC *TableOfContents;
};

int megg_getEggInfo(unsigned char* fileBytes, unsigned int length, megg_info* result);


#endif // MONDEGREENGAMES_EGG_H

#ifdef MONDEGREENGAMES_EGG_IMPLEMENTATION

#include <stdint.h>

int megg_getEggInfo(unsigned char* fileBytes, unsigned int length, megg_info* result)
{
	static_assert(sizeof(megg_info::Filename) == 1, "megg_info::Filename is unexpected size");

	struct header
	{
		char Magic[4];
		unsigned short Version;
		unsigned short Flags;
		uint64_t Timestamp;
		unsigned int NumFiles;
		unsigned int FilenameOffset;
		unsigned int TOCOffset;
		unsigned int Unused;
	};

	if (length < sizeof(header))
		return -1;

	header* h = (header*)fileBytes;
	if (h->Magic[0] != 'E' || h->Magic[1] != 'G' || h->Magic[2] != 'G' || h->Magic[3] != 'A')
		return -1;

	if (h->FilenameOffset > length
		|| h->TOCOffset > length
		|| h->TOCOffset + sizeof(megg_info::TOC) * h->NumFiles > length
		|| h->FilenameOffset + h->NumFiles > length)
		return -1;

	struct megg_info::TOC* toc = (megg_info::TOC*)(fileBytes + h->TOCOffset);

	result->NumFiles = h->NumFiles;
	result->TableOfContents = toc;
	result->Filenames = (megg_info::Filename*)(fileBytes + h->FilenameOffset);

	// do a quick validation of the filenames and TOC
	auto filenameCursor = result->Filenames;
	for (unsigned int i = 0; i < h->NumFiles; i++)
	{
		if (filenameCursor->Name + filenameCursor->Length + 1 > (char*)fileBytes + length)
			return -1;
		if (filenameCursor->Name[filenameCursor->Length] != 0)
			return -1;
		filenameCursor += filenameCursor->Length + 2;

		if (result->TableOfContents[i].FileContentOffset + result->TableOfContents[i].CompressedSize > length)
			return -1;
	}

	return 0;
}

#endif // MONDEGREENGAMES_EGG_IMPLEMENTATION