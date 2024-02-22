#ifndef ENGINE_SHARED_UUID_H
#define ENGINE_SHARED_UUID_H

enum
{
	UUID_MAXSTRSIZE = 37, // 12345678-0123-5678-0123-567890123456

	UUID_INVALID = -2,
	UUID_UNKNOWN = -1,

	OFFSET_UUID = 1 << 16,
};

struct CUuid
{
	unsigned char m_aData[16];

	bool operator==(const CUuid &Other) const;
	bool operator!=(const CUuid &Other) const;
	bool operator<(const CUuid &Other) const;
};

extern const CUuid UUID_ZEROED;

CUuid RandomUuid();
CUuid CalculateUuid(const char *pName);
// The buffer length should be at least UUID_MAXSTRSIZE.
void FormatUuid(CUuid Uuid, char *pBuffer, unsigned BufferLength);

#endif // ENGINE_SHARED_UUID_H
