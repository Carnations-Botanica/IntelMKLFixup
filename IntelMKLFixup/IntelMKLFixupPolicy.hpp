//
//  IntelMKLFixupPolicy.hpp
//  IntelMKLFixup
//
//  Pure, allocation-free policy and byte matching helpers. This header must
//  remain usable by host-side tests without Lilu or kernel headers.
//

#ifndef IntelMKLFixupPolicy_hpp
#define IntelMKLFixupPolicy_hpp

#include <stddef.h>
#include <stdint.h>

namespace IMKLFX {

static constexpr size_t MaximumPathLength {1024};

enum class TargetState : uint8_t {
	NotCovered,
	Original,
	AlreadyPatched,
	Mismatch
};

struct PatchDefinition {
	const char *identifier;
	const char *name;
	const char *architecture;
	const uint8_t *search;
	size_t searchSize;
	const uint8_t *searchMask;
	size_t searchMaskSize;
	const uint8_t *replacement;
	size_t replacementSize;
	const uint8_t *replacementMask;
	size_t replacementMaskSize;
	const uint8_t *contextBefore;
	size_t contextBeforeSize;
	const uint8_t *contextAfter;
	size_t contextAfterSize;
	const char *knownMklGeneration;
	const char *knownApplications;
};

struct ImageVariant {
	const char *identifier;
	const char *applicationIdentifier;
	const char *signingIdentifier;
	const char *teamIdentifier;
	const uint8_t *codeDirectoryHash;
	size_t codeDirectoryHashSize;
	uint64_t targetFileOffset;
	uint64_t executableTextStart;
	uint64_t executableTextEnd;
	const PatchDefinition *patch;
};

static constexpr uint8_t MklServIntelCpuTrueSearchV1[] = {
	0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x35, 0x61,
	0x0F, 0x79, 0x00, 0x85, 0xF6, 0x7C, 0x08, 0x89,
	0xF0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3
};

static constexpr uint8_t MklServIntelCpuTrueReplacementV1[] = {
	0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3
};

static constexpr uint8_t MklServIntelCpuTrueContextBeforeV1[] = {
	0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
	0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
};

static constexpr uint8_t MklServIntelCpuTrueContextAfterV1[] = {
	0x33, 0xF6, 0x89, 0x74, 0x24, 0x18, 0x89, 0x34,
	0x24, 0x8B, 0x04, 0x24, 0x8B, 0x4C, 0x24, 0x18
};

static constexpr PatchDefinition MklServIntelCpuTrueDiscordV1 {
	"mkl-serv-intel-cpu-true-x86_64-discord-v1",
	"Intel oneAPI MKL cached CPU-vendor gate",
	"x86_64",
	MklServIntelCpuTrueSearchV1,
	sizeof(MklServIntelCpuTrueSearchV1),
	nullptr,
	0,
	MklServIntelCpuTrueReplacementV1,
	sizeof(MklServIntelCpuTrueReplacementV1),
	nullptr,
	0,
	MklServIntelCpuTrueContextBeforeV1,
	sizeof(MklServIntelCpuTrueContextBeforeV1),
	MklServIntelCpuTrueContextAfterV1,
	sizeof(MklServIntelCpuTrueContextAfterV1),
	"Intel oneAPI MKL build 20201104",
	"Discord Stable 0.0.403 discord_krisp.node (source evidence; runtime untested)"
};

// This is the embedded x86_64 CodeDirectory hash in the preserved original
// Discord Stable 0.0.403 module. XNU page validation and CS_VALID are still
// required: the local backup itself currently fails codesign verification and
// is not treated as a trusted runnable file.
static constexpr uint8_t DiscordStable00403KrispCdHash[] = {
	0x58, 0x5E, 0x95, 0x75, 0xA8, 0x70, 0xDB, 0x3F, 0x7D, 0xE0,
	0xE9, 0xD3, 0xC7, 0x4F, 0xC9, 0xD7, 0xE7, 0xF0, 0x84, 0xCD
};

static constexpr ImageVariant DiscordStable00403KrispX8664 {
	"discord-stable-0.0.403-krisp-x86_64-585e9575",
	"discord-stable-krisp",
	"discord_krisp",
	"53Q6R32WPB",
	DiscordStable00403KrispCdHash,
	sizeof(DiscordStable00403KrispCdHash),
	0x650100,
	0x4D00,
	0xCBCED0,
	&MklServIntelCpuTrueDiscordV1
};

static_assert(sizeof(MklServIntelCpuTrueReplacementV1) <= sizeof(MklServIntelCpuTrueSearchV1),
	"replacement must fit in the verified target");
static_assert(sizeof(DiscordStable00403KrispCdHash) == 20,
	"CodeDirectory hashes must use XNU's 20-byte CDHash representation");
static_assert(DiscordStable00403KrispX8664.targetFileOffset >= DiscordStable00403KrispX8664.executableTextStart,
	"target must start inside __TEXT,__text");
static_assert(DiscordStable00403KrispX8664.targetFileOffset + sizeof(MklServIntelCpuTrueSearchV1) <= DiscordStable00403KrispX8664.executableTextEnd,
	"target must end inside __TEXT,__text");

inline bool bytesEqual(const uint8_t *left, const uint8_t *right, size_t size) {
	if (size == 0)
		return true;
	if (left == nullptr || right == nullptr)
		return false;
	for (size_t i = 0; i < size; i++) {
		if (left[i] != right[i])
			return false;
	}
	return true;
}

inline bool consumeLiteral(const char *&cursor, const char *end, const char *literal) {
	if (cursor == nullptr || end == nullptr || literal == nullptr || cursor > end)
		return false;
	for (size_t i = 0; literal[i] != '\0'; i++) {
		if (cursor == end || *cursor != literal[i])
			return false;
		cursor++;
	}
	return true;
}

inline bool consumeDecimal(const char *&cursor, const char *end, char delimiter) {
	if (cursor == nullptr || end == nullptr || cursor >= end)
		return false;
	size_t digits = 0;
	while (cursor < end && *cursor >= '0' && *cursor <= '9') {
		if (++digits > 10)
			return false;
		cursor++;
	}
	if (digits == 0 || cursor == end || *cursor != delimiter)
		return false;
	cursor++;
	return true;
}

inline bool matchDiscordStableKrispPath(const char *path, size_t length) {
	if (path == nullptr || length == 0 || length > MaximumPathLength)
		return false;

	const char *cursor = path;
	const char *end = path + length;
	if (!consumeLiteral(cursor, end, "/Users/"))
		return false;

	const char *account = cursor;
	while (cursor < end && *cursor != '/')
		cursor++;
	const size_t accountLength = static_cast<size_t>(cursor - account);
	if (accountLength == 0 || accountLength > 255 || cursor == end)
		return false;
	if ((accountLength == 1 && account[0] == '.') ||
		(accountLength == 2 && account[0] == '.' && account[1] == '.'))
		return false;

	if (!consumeLiteral(cursor, end, "/Library/Application Support/discord/app-"))
		return false;
	if (!consumeDecimal(cursor, end, '.'))
		return false;
	if (!consumeDecimal(cursor, end, '.'))
		return false;
	if (!consumeDecimal(cursor, end, '/'))
		return false;
	if (!consumeLiteral(cursor, end, "modules/discord_krisp-"))
		return false;
	if (!consumeDecimal(cursor, end, '/'))
		return false;

	const char *direct = cursor;
	if (consumeLiteral(direct, end, "discord_krisp.node") && direct == end)
		return true;

	return consumeLiteral(cursor, end, "discord_krisp/discord_krisp.node") && cursor == end;
}

inline TargetState classifyTarget(const uint8_t *data, size_t dataSize, uint64_t rangeOffset,
	const ImageVariant &variant) {
	const PatchDefinition *patch = variant.patch;
	if (data == nullptr || patch == nullptr || patch->search == nullptr ||
		patch->replacement == nullptr || patch->replacementSize == 0 ||
		patch->replacementSize > patch->searchSize ||
		patch->searchMask != nullptr || patch->searchMaskSize != 0 ||
		patch->replacementMask != nullptr || patch->replacementMaskSize != 0)
		return TargetState::Mismatch;

	if (variant.targetFileOffset < variant.executableTextStart ||
		variant.targetFileOffset > variant.executableTextEnd ||
		patch->searchSize > variant.executableTextEnd - variant.targetFileOffset)
		return TargetState::Mismatch;

	if (rangeOffset > variant.targetFileOffset)
		return TargetState::NotCovered;
	const uint64_t relative64 = variant.targetFileOffset - rangeOffset;
	if (relative64 > static_cast<uint64_t>(dataSize))
		return TargetState::NotCovered;
	const size_t relative = static_cast<size_t>(relative64);
	if (patch->contextBeforeSize > relative)
		return TargetState::NotCovered;
	if (patch->searchSize > dataSize - relative)
		return TargetState::NotCovered;
	const size_t afterOffset = relative + patch->searchSize;
	if (patch->contextAfterSize > dataSize - afterOffset)
		return TargetState::NotCovered;

	if (!bytesEqual(data + relative - patch->contextBeforeSize,
		patch->contextBefore, patch->contextBeforeSize) ||
		!bytesEqual(data + afterOffset, patch->contextAfter, patch->contextAfterSize))
		return TargetState::Mismatch;

	if (bytesEqual(data + relative, patch->search, patch->searchSize))
		return TargetState::Original;

	if (bytesEqual(data + relative, patch->replacement, patch->replacementSize) &&
		bytesEqual(data + relative + patch->replacementSize,
			patch->search + patch->replacementSize,
			patch->searchSize - patch->replacementSize))
		return TargetState::AlreadyPatched;

	return TargetState::Mismatch;
}

inline uint8_t *targetPointer(uint8_t *data, size_t dataSize, uint64_t rangeOffset,
	const ImageVariant &variant) {
	if (data == nullptr || rangeOffset > variant.targetFileOffset)
		return nullptr;
	const uint64_t relative = variant.targetFileOffset - rangeOffset;
	if (relative > static_cast<uint64_t>(dataSize))
		return nullptr;
	const size_t offset = static_cast<size_t>(relative);
	if (variant.patch == nullptr || variant.patch->replacementSize > dataSize - offset)
		return nullptr;
	return data + offset;
}

} // namespace IMKLFX

#endif /* IntelMKLFixupPolicy_hpp */
