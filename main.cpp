#include "PlatformWrap.h"

#include "HashFunctions/CStringHash.h"
#include "HashFunctions/FNVHash.h"
#include "HashFunctions/HashFunctions.h"
#include "HashFunctions/MurmurHash2.h"
#include "HashFunctions/MurmurHash3.h"
#include "HashFunctions/SpookyV2.h"
#include "HashFunctions/xxhash.h"

#include <vector>
#include <string>
#include <set>
#include <map>
#include <stdio.h>


FILE* g_OutputFile = stdout;

extern void crc32 (const void * key, int len, uint32_t seed, void * out);


// ------------------------------------------------------------------------------------
// Data sets & reading them from file


typedef std::vector<std::string> WordList;

static WordList g_Words;
static size_t g_TotalSize;

static std::string g_SyntheticData;

static void ReadWords(const char* filename)
{
	g_Words.clear();
	FILE* f = fopen(filename, "rb");
	if (!f)
	{
		fprintf(g_OutputFile, "error: can't open dictionary file '%s'\n", filename);
		return;
	}
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* buffer = new char[size];
	fread(buffer, size, 1, f);

	size_t pos = 0;
	size_t wordStart = 0;
	g_TotalSize = 0;
	while (pos < size)
	{
		if (buffer[pos] == '\n')
		{
			std::string word;
			word.assign(buffer+wordStart, pos-wordStart);
			// remove any trailing windows newlines
			while (!word.empty() && word[word.size()-1] == '\r')
				word.pop_back();
			g_Words.push_back(word);
			g_TotalSize += word.size();
			wordStart = pos+1;
		}
		++pos;
	}
	
	delete[] buffer;
	fclose(f);
}


// ------------------------------------------------------------------------------------
// Hash function testing code

inline uint32_t NextPowerOfTwo(uint32_t v)
{
	v -= 1;
	v |= v >> 16;
	v |= v >> 8;
	v |= v >> 4;
	v |= v >> 2;
	v |= v >> 1;
	return v + 1;
}


template<typename Hasher>
void TestOnData(const Hasher& hasher, const char* name)
{
	// hash all the entries; do several iterations and pick smallest time
	typename Hasher::HashType hashsum = 0x1234;
	float minsec = 1.0e6f;
	for (int iterations = 0; iterations < 5; ++iterations)
	{
		TimerBegin();
		for (size_t i = 0, n = g_Words.size(); i != n; ++i)
		{
			const std::string& s = g_Words[i];
			hashsum ^= hasher(s.data(), s.size());
		}
		float sec = TimerEnd();
		if (sec < minsec)
			minsec = sec;
	}
	// MB/s on real data
	double mbps = (g_TotalSize / 1024.0 / 1024.0) / minsec;

	// test for "hash quality":
	// unique hashes found in all the entries (#entries - uniq == how many collisions found)
	std::set<typename Hasher::HashType> uniq;
	// unique buckets that we'd end up with, if we had a hashtable with a load factor of 0.8 that is
	// always power of two size.
	std::map<typename Hasher::HashType, int> uniqModulo;
	size_t hashtableSize = NextPowerOfTwo(g_Words.size() / 0.8);
	int maxBucket = 0;
	for (size_t i = 0, n = g_Words.size(); i != n; ++i)
	{
		const std::string& s = g_Words[i];
		typename Hasher::HashType h = hasher(s.data(), s.size());
		uniq.insert(h);
		int bucketSize = uniqModulo[h % hashtableSize]++;
		if (bucketSize > maxBucket)
			maxBucket = bucketSize;
	}
	size_t collisions = g_Words.size() - uniq.size();
	size_t collisionsHashtable = g_Words.size() - uniqModulo.size();
	double avgBucket = (double)g_Words.size() / uniqModulo.size();

	// use hashsum in a fake way so that it's not completely compiled away by the optimizer
	mbps += (hashsum & 0x7) * 0.0001;
	fprintf(g_OutputFile, "%15s: %8.0f MB/s, %5i collis, %5i htcollis %i max %.3f avgbucket\n", name, mbps, (int)collisions, (int)collisionsHashtable, maxBucket, avgBucket);
}


template<typename Hasher>
void TestHashPerformance(const Hasher& hasher, const char* name)
{
	// synthetic hash performance test on various string lengths
	int step = 2;
	for (int len = 2; len < 4000; len += step, step += step/2)
	{
		typename Hasher::HashType hashsum = 0x1234;
		size_t dataLen = g_SyntheticData.size();
		// do several iterations and pick smallest time
		float minsec = 1.0e6f;
		size_t totalBytes = 0;
		for (int iterations = 0; iterations < 1; ++iterations)
		{
			const char* dataPtr = g_SyntheticData.data();
			TimerBegin();
			size_t pos = 0;
			while (pos + len < dataLen)
			{
				hashsum ^= hasher(dataPtr + pos, len);
				pos += len;
			}
			float sec = TimerEnd();
			totalBytes = pos;
			if (sec < minsec)
				minsec = sec;
		}
		// MB/s
		double mbps = (totalBytes / 1024.0 / 1024.0) / minsec;

		// use hashsum in a fake way so that it's not completely compiled away by the optimizer
		fprintf(g_OutputFile, "%15s: len %4i %8.0f MB/s\n", name, len, mbps + (hashsum & 7)*0.00001);
	}
}


// ------------------------------------------------------------------------------------
// Individual hash functions for use in the testing code above

struct HasherXXH32
{
	typedef uint32_t HashType;
	HashType operator()(const void* data, size_t size) const
	{
		return XXH32(data, size, 0x1234);
	}
};

struct HasherXXH64_32
{
	typedef uint32_t HashType;
	HashType operator()(const void* data, size_t size) const
	{
		return (HashType)XXH64(data, size, 0x1234);
	}
};

struct HasherXXH64
{
	typedef uint64_t HashType;
	HashType operator()(const void* data, size_t size) const
	{
		return XXH64(data, size, 0x1234);
	}
};

struct HasherSpookyV2_64
{
	typedef uint64_t HashType;
	HashType operator()(const void* data, size_t size) const
	{
		return SpookyHash::Hash64(data, (int)size, 0x1234);
	}
};

struct HasherMurmur2A
{
	typedef uint32_t HashType;
	HashType operator()(const void* data, size_t size) const
	{
		return MurmurHash2A(data, (int)size, 0x1234);
	}
};

struct HasherMurmur3_32
{
	typedef uint32_t HashType;
	HashType operator()(const void* data, size_t size) const
	{
		HashType res;
		MurmurHash3_x86_32(data, (int)size, 0x1234, &res);
		return res;
	}
};

struct HasherFNV
{
	typedef uint32_t HashType;
	HashType operator()(const void* data, size_t size) const
	{
		return FNVHash((const char*)data, (int)size);
	}
};

struct HasherCRC32
{
	typedef uint32_t HashType;
	HashType operator()(const void* data, size_t size) const
	{
		HashType res;
		crc32(data, (int)size, 0x1234, &res);
		return res;
	}
};

struct HasherBadHashFunctionWeUsedToHave
{
	typedef uint32_t HashType;
	HashType operator()(const void* data, size_t size) const
	{
		return hash_cstring()((const char*)data, (int)size);
	}
};


// ------------------------------------------------------------------------------------
// Main program

#define TEST_HASHES(TestFunction) \
	TestFunction(HasherXXH32(), "xxHash32"); \
	TestFunction(HasherXXH64_32(), "xxHash64-32"); \
	TestFunction(HasherXXH64(), "xxHash64"); \
	TestFunction(HasherSpookyV2_64(), "SpookyV2-64"); \
	TestFunction(HasherMurmur2A(), "Murmur2A"); \
	TestFunction(HasherMurmur3_32(), "Murmur3-32"); \
	/*TestFunction(HasherCRC32(), "CRC32");*/ \
	TestFunction(HasherFNV(), "FNV"); \
	TestFunction(djb2_hash(), "djb2"); \
	/*TestFunction(HasherBadHashFunctionWeUsedToHave(), "BadHash")*/;


static void DoTestOnRealData(const char* folderName, const char* filename)
{
	std::string fullPath = std::string(folderName) + filename;

	ReadWords(fullPath.c_str());
	if (g_Words.empty())
		return;
	fprintf(g_OutputFile, "Testing on %s: %i entries (%.1f MB size, avg length %.1f)\n", filename, (int)g_Words.size(), g_TotalSize / 1024.0 / 1024.0, double(g_TotalSize) / g_Words.size());
	TEST_HASHES(TestOnData);
	g_Words.clear();
}


static void DoTestSyntheticData()
{
#if PLATFORM_WEBL || PLATFORM_XBOXONE || PLATFORM_PS4
	const size_t kSize = 1024 * 1024 * 64;
#else
	const size_t kSize = 1024 * 1024 * 128;
#endif
	g_SyntheticData.resize(kSize);
	for (size_t i = 0; i < kSize; ++i)
		g_SyntheticData[i] = i;
	fprintf(g_OutputFile, "Testing on synthetic data\n");
	TEST_HASHES(TestHashPerformance);
	g_SyntheticData.clear();
}

extern "C" void HashFunctionsTestEntryPoint(const char* folderName)
{
	// Basic collisions / hash quality tests on some real world data I had lying around:
	// - Dictionary of English words from /usr/share/dict/words
	// - A bunch of file relative paths + filenames from several Unity projects & test suites.
	//   Imaginary use case, hashing filenames in some asset database / file storage system.
	// - C++ source code, this was partial Unity sourcecode dump. I'm not releasing this one :),
	//   but it was 6069 entries, 43.7MB total size, average size 7546.6 bytes.
	// - Mostly binary data. I instrumented hash function calls, as used in Unity engine graphics
	//   related parts, to dump actually hashed data into a log file. Unlike the test sets above,
	//   most of the data here is binary, and represents snapshots of some internal structs in
	//   memory.
	//DoTestOnRealData(folderName, "TestData/test-words.txt");
	//DoTestOnRealData(folderName, "TestData/test-filenames.txt");
	//DoTestOnRealData(folderName, "TestData/test-code.txt");
	//DoTestOnRealData(folderName, "TestData/test-binary.bin");
	DoTestSyntheticData();
}


// iOS & XB1 has main entry points elsewhere
#if !PLATFORM_IOS && !PLATFORM_XBOXONE

int main()
{
	#if PLATFORM_PS4
	const char* folderName = "/app0/";
	#else
	const char* folderName = "";
	#endif
	HashFunctionsTestEntryPoint(folderName);
	return 0;
}

#endif // #if !PLATFORM_IOS && !PLATFORM_XBOXONE

