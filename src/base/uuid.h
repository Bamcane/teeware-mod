#ifndef BASE_UUID_H
#define BASE_UUID_H

#include <functional>

enum
{
	UUID_MAXSTRSIZE = 37, // 12345678-0123-5678-0123-567890123456

	UUID_INVALID = -2,
	UUID_UNKNOWN = -1,

	OFFSET_UUID = 1 << 16,
};

struct Uuid
{
	unsigned char m_aData[16];

	bool operator==(const Uuid &Other) const;
	bool operator!=(const Uuid &Other) const;
	bool operator<(const Uuid &Other) const;
};

extern const Uuid UUID_ZEROED;

Uuid RandomUuid();
Uuid CalculateUuid(const char *pName);
// The buffer length should be at least UUID_MAXSTRSIZE.
void FormatUuid(Uuid Uuid, char *pBuffer, unsigned BufferLength);
// Returns nonzero on failure.
int ParseUuid(Uuid *pUuid, const char *pBuffer);

namespace std
{
    template<>
    struct hash<Uuid>
	{
        size_t operator()(const Uuid& uuid) const noexcept;
    };
}

#endif // BASE_UUID_H From DDNet
