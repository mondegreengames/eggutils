#include <cstdio>
#include <vector>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "lz4.h"
#include "lz4hc.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef CopyFile
#undef GetCurrentTime
#else
//#error Not supported yet!

#endif

typedef uint8_t uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef uint64_t uint64;

struct FileInfo
{
	const char* Name;
	uint32 Index;
	uint32 Offset;
	uint32 UncompressedSize;
	uint32 CompressedSize;
};

const uint32 OffsetOfFilenameOffset = 20;
const uint32 OffsetOfTOCOffset = 24;

bool CopyFile(FILE* output, const char* input, uint32* uncompressedSize, uint32* compressedSize)
{
#ifdef _WIN32
	FILE* fp;
	fopen_s(&fp, input, "rb");
#else
	FILE* fp = fopen(input, "rb");
#endif
	if (fp == nullptr)
	{
		printf("Unable to open %s", input);
		return false;
	}

	// get the total size
	fseek(fp, 0, SEEK_END);
	uint32 size = (uint32)ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// allocate a buffer
	uint8* fileBuffer = new uint8[size];
	if (fread(fileBuffer, 1, size, fp) != size)
		return false;

	// attempt to compress it
	uint32 compressedBufferSize = LZ4_compressBound(size);
	uint8* compressedBuffer = new uint8[compressedBufferSize];
	int r = LZ4_compress_HC((char*)fileBuffer, (char*)compressedBuffer, size, compressedBufferSize, 0);
	if (true || r <= 0 || r > size * 3 / 4 || size < 1024 * 10)
	{
		// compression didn't work or it wasn't worth it
		fwrite(fileBuffer, 1, size, output);
		*compressedSize = size;
	}
	else
	{
		// write the compressed version
		fwrite(compressedBuffer, 1, r, output);
		*compressedSize = r;
	}

	fclose(fp);

	*uncompressedSize = size;

	delete[] fileBuffer;
	delete[] compressedBuffer;

	return true;
}

uint64 GetCurrentTime()
{
#ifdef _WIN32
	static_assert(sizeof(FILETIME) == sizeof(uint64), "FILETIME has unexpected size");

	FILETIME time;
	GetSystemTimeAsFileTime(&time);

	uint64 result;
	memcpy(&result, &time, sizeof(uint64));

	return result;
#else
	assert(false && "Not supported yet");
#endif
}

int compare(const void* c1, const void* c2)
{
	FileInfo* f1 = (FileInfo*)c1;
	FileInfo* f2 = (FileInfo*)c2;

#ifdef _WIN32
	return _stricmp(f1->Name, f2->Name);
#else
	return strcasecmp(f1->Name, f2->Name);
#endif
}

int build(const char* output, const char* const* inputs, uint32 numInputs)
{
	if (numInputs == 0)
	{
		printf("At least one input is required\n");
		return -1;
	}

#ifdef _WIN32
	FILE* out;
	fopen_s(&out, output, "wb");
#else
	FILE* out = fopen(output, "wb");
#endif
	if (out == nullptr)
	{
		printf("Unable to open %s for output\n", output);
		return -1;
	}

	// write the header
	{
		char magic[4] = { 'E', 'G', 'G', 'A' }; // EGG Archive
		fwrite(magic, 4, 1, out);

		uint16 version = 1;
		uint16 flags = 0;
		fwrite(&version, 2, 1, out);
		fwrite(&flags, 2, 1, out);

		uint64 time = GetCurrentTime();
		fwrite(&time, 8, 1, out);
		fwrite(&numInputs, 4, 1, out);

		uint32 dummy = 0;
		fwrite(&dummy, 4, 1, out);
		fwrite(&dummy, 4, 1, out);
		fwrite(&dummy, 4, 1, out);

		auto offset = ftell(out);
		assert(offset % 8 == 0);
	}

	// begin writing the files
	FileInfo* files = new FileInfo[numInputs];
	for (uint32 i = 0; i < numInputs; i++)
	{
		files[i].Name = inputs[i];
		files[i].Index = i;
		files[i].Offset = ftell(out);

		if (CopyFile(out, inputs[i], &files[i].UncompressedSize, &files[i].CompressedSize) == false)
		{
			printf("Error copying %s into output\n", inputs[i]);

			fclose(out);
			return -1;
		}

		// keep everything aligned to 8 byte offset
		auto offset = ftell(out);
		auto padding = (8 - (offset % 8)) % 8;
		if (padding > 0)
		{
			uint64 dummy = 0;
			fwrite(&dummy, padding, 1, out);
		}

		if (files[i].CompressedSize < files[i].UncompressedSize)
			printf("Added %s (%u bytes compressed to %u) to %s\n", inputs[i], files[i].UncompressedSize, files[i].CompressedSize, output);
		else
			printf("Added %s (%u bytes) to %s\n", inputs[i], files[i].UncompressedSize, output);
	}

	// alphabetize the filenames
	qsort(files, numInputs, sizeof(FileInfo), compare);

	// write table of contents
	uint32 offsetOfTOC = (uint32)ftell(out);
	assert(offsetOfTOC % 8 == 0);
	{
		// write the file info
		for (uint32 i = 0; i < numInputs; i++)
		{
			uint32 flags = 0;

			// 0x01 means LZ4 compressed
			if (files[i].CompressedSize < files[i].UncompressedSize)
				flags = 0x01;

			fwrite(&files[i].Offset, 4, 1, out);
			fwrite(&files[i].CompressedSize, 4, 1, out);
			fwrite(&files[i].UncompressedSize, 4, 1, out);
			fwrite(&flags, 4, 1, out);
		}
	}

	// write filenames
	uint32 offsetOfFilenames = (uint32)ftell(out);
	assert(offsetOfFilenames % 8 == 0);
	{
		for (uint32 i = 0; i < numInputs; i++)
		{
			uint32 len = strlen(files[i].Name);
			assert(len <= 255);

			uint8 blen = (uint8)len;
			fwrite(&blen, 1, 1, out);
			fwrite(files[i].Name, len + 1, 1, out);
		}
	}

	// go back and write the offsets
	fseek(out, OffsetOfTOCOffset, SEEK_SET);
	fwrite(&offsetOfTOC, 4, 1, out);
	fseek(out, OffsetOfFilenameOffset, SEEK_SET);
	fwrite(&offsetOfFilenames, 4, 1, out);

	fclose(out);

	return 0;
}

int list(const char* egg)
{
#ifdef _WIN32
	FILE* fp;
	fopen_s(&fp, egg, "rb");
#else
	FILE* fp = fopen(egg, "rb");
#endif
	if (fp == nullptr)
	{
		printf("Unable to open %s\n", egg);
		return false;
	}

#pragma pack(1)
	struct
	{
		char Magic[4];
		unsigned short FormatVersion;
		unsigned short Flags;
		uint64_t BuildDate;
		unsigned int NumFiles;
		unsigned int OffsetToFilenames;
		unsigned int OffsetToTableOfContents;
		unsigned int Reserved;
	} header;

	struct
	{
		unsigned int OffsetToFile;
		unsigned int CompressedSizeOfFile;
		unsigned int UncompressedSizeOfFile;
		unsigned int Flags;
	} toc;
#pragma pack()

	fread(&header, sizeof(header), 1, fp);

	// do some validation
	if (header.Magic[0] != 'E' ||
		header.Magic[1] != 'G' ||
		header.Magic[2] != 'G' ||
		header.Magic[3] != 'A')
	{
		fclose(fp);
		return -1;
	}

	// jump to the filenames
	fseek(fp, header.OffsetToFilenames, SEEK_SET);

	char buffer[256];
	for (uint32 i = 0; i < header.NumFiles; i++)
	{
		uint8 len;
		fread(&len, 1, 1, fp);

		fread(buffer, len + 1, 1, fp);

		puts(buffer);
	}

	fclose(fp);
	return 0;
}

int extract(const char* egg, const char* file)
{
#ifdef _WIN32
	FILE* fp;
	fopen_s(&fp, egg, "rb");
#else
	FILE* fp = fopen(egg, "rb");
#endif
	if (fp == nullptr)
	{
		printf("Unable to open %s\n", egg);
		return false;
	}
	
#pragma pack(1)
	struct
	{
		char Magic[4];
		unsigned short FormatVersion;
		unsigned short Flags;
		uint64_t BuildDate;
		unsigned int NumFiles;
		unsigned int OffsetToFilenames;
		unsigned int OffsetToTableOfContents;
		unsigned int Reserved;
	} header;

	struct
	{
		unsigned int OffsetToFile;
		unsigned int CompressedSizeOfFile;
		unsigned int UncompressedSizeOfFile;
		unsigned int Flags;
	} toc;
#pragma pack()

	fread(&header, sizeof(header), 1, fp);

	// do some validation
	if (header.Magic[0] != 'E' ||
		header.Magic[1] != 'G' ||
		header.Magic[2] != 'G' ||
		header.Magic[3] != 'A')
	{
		fclose(fp);
		return -1;
	}

	// jump to the filenames
	fseek(fp, header.OffsetToFilenames, SEEK_SET);

	char buffer[256];
	for (uint32 i = 0; i < header.NumFiles; i++)
	{
		uint8 len;
		fread(&len, 1, 1, fp);

		fread(buffer, len + 1, 1, fp);

#ifdef _WIN32
		if (_stricmp(buffer, file) == 0)
#else
		if (strcasecmp(buffer, file) == 0)
#endif
		{
			// found it!
			const char* filename = buffer;
			auto slash = strrchr(filename, '/');
			if (slash != nullptr)
				filename = slash + 1;
#ifdef _WIN32
			FILE* out;
			fopen_s(&out, filename, "wb");
#else
			FILE* out = fopen(filename, "wb");
#endif
			if (out == nullptr)
			{
				printf("Unable to open %s for writing\n", filename);
				fclose(fp);
				return -1;
			}

			fseek(fp, header.OffsetToTableOfContents + sizeof(toc) * i, SEEK_SET);
			fread(&toc, sizeof(toc), 1, fp);

			fseek(fp, toc.OffsetToFile, SEEK_SET);
			const uint32 extractBufferSize = 1024 * 4;
			char extractBuffer[extractBufferSize];

			uint32 totalBytesRead = 0;
			while (true)
			{
				uint32 bytesRemaining = toc.UncompressedSizeOfFile - totalBytesRead;
				uint32 bytesToRead = bytesRemaining < extractBufferSize ? bytesRemaining : extractBufferSize;

				uint32 bytesRead = fread(extractBuffer, 1, bytesToRead, fp);
				fwrite(extractBuffer, 1, bytesRead, out);

				totalBytesRead += bytesRead;

				if (bytesRead == 0)
					break;
			}

			fclose(out);
			fclose(fp);
			return 0;
		}
	}

	// still here? guess we couldn't find it
	printf("Unable to find %s within %s\n", file, egg);
	fclose(fp);
	return -1;
}

int main(int argc, char* argv[])
{	
	const char* command = argv[1];
	const char* eggFile = argv[2];

	if (argc <= 2)
	{
		goto printUsage;
	}
	
	if (strcmp(command, "build") == 0)
	{
		if (argc <= 3)
		{
			printf("The egg needs at least one file.\n");
			goto printUsage;
		}

		return build(eggFile, &argv[3], argc - 3);
	}
	else if (strcmp(command, "extract") == 0)
	{
		if (argc < 4)
		{
			printf("What file do you want to extract?\n");
			goto printUsage;
		}

		return extract(eggFile, argv[3]);
	}
	else if (strcmp(command, "list") == 0)
	{
		return list(eggFile);
	}
	else
	{
		goto printUsage;
	}


printUsage:
	printf("Usage:\n");
	printf("EggArchiveBuilder build [OUTPUT] [input files]\n");
	printf("EggArchiveBuilder extract [egg file] [file to extract]\n");
	printf("EggArchiveBuilder list [egg file]\n");
	printf("\n");

	return 0;
}
