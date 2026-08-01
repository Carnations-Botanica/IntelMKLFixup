//
//  IntelMKLFixupCatalogue.hpp
//  IntelMKLFixup
//
//  Reviewed built-in PatchDefinitions, ApplicationRules, and ImageVariants.
//  Application-specific data belongs here, never in the policy engine.
//

#ifndef IntelMKLFixupCatalogue_hpp
#define IntelMKLFixupCatalogue_hpp

#include "IntelMKLFixupPolicy.hpp"

namespace IMKLFX {

static constexpr uint64_t InitialX8664ValidationRangeSize {4096};

inline bool consumeLiteral(const char *&cursor, const char *end,
	const char *literal) {
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

// Discord-specific path grammar. It is one ApplicationRule implementation,
// not a condition in the generic policy selector.
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
	return consumeLiteral(cursor, end,
		"discord_krisp/discord_krisp.node") && cursor == end;
}

static constexpr uint8_t MklServIntelCpuTrueOneApiBuild20201104Search[] = {
	0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x35, 0x61,
	0x0F, 0x79, 0x00, 0x85, 0xF6, 0x7C, 0x08, 0x89,
	0xF0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3
};

static constexpr uint8_t MklServIntelCpuTrueReturnTrueX8664[] = {
	0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3
};

static constexpr uint8_t MklServIntelCpuTrueOneApiBuild20201104ContextBefore[] = {
	0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
	0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
};

static constexpr uint8_t MklServIntelCpuTrueOneApiBuild20201104ContextAfter[] = {
	0x33, 0xF6, 0x89, 0x74, 0x24, 0x18, 0x89, 0x34,
	0x24, 0x8B, 0x04, 0x24, 0x8B, 0x4C, 0x24, 0x18
};

static constexpr PatchDefinition MklServIntelCpuTrueOneApiBuild20201104X8664 {
	"mkl-serv-intel-cpu-true-oneapi-build-20201104-x86_64-v1",
	"Intel oneAPI MKL build 20201104 cached CPU-vendor gate",
	"x86_64",
	MklServIntelCpuTrueOneApiBuild20201104Search,
	sizeof(MklServIntelCpuTrueOneApiBuild20201104Search),
	nullptr,
	0,
	MklServIntelCpuTrueReturnTrueX8664,
	sizeof(MklServIntelCpuTrueReturnTrueX8664),
	nullptr,
	0,
	MklServIntelCpuTrueOneApiBuild20201104ContextBefore,
	sizeof(MklServIntelCpuTrueOneApiBuild20201104ContextBefore),
	MklServIntelCpuTrueOneApiBuild20201104ContextAfter,
	sizeof(MklServIntelCpuTrueOneApiBuild20201104ContextAfter),
	PatchValidationPolicy::ExactBytesAndContext,
	"Intel oneAPI MKL build 20201104",
	"Exact function bytes and exact before/after context; replacement is mov eax, 1; ret"
};

static constexpr ApplicationRule DiscordStableKrispApplication {
	"discord-stable-krisp",
	"Discord Stable Krisp native module",
	"discord-stable-krisp-path-v1",
	"discord_krisp.node",
	sizeof("discord_krisp.node") - 1,
	matchDiscordStableKrispPath,
	"discord_krisp",
	sizeof("discord_krisp") - 1,
	TeamIdentifierPolicy::Exact,
	"53Q6R32WPB",
	sizeof("53Q6R32WPB") - 1,
	CodeSigningPolicy::ValidRuntimeNotAdHoc
};

// Embedded x86_64 CodeDirectory hash for the reviewed original Discord Stable
// 0.0.403 variant. It is application evidence, not part of the MKL definition.
static constexpr uint8_t DiscordStable00403KrispCdHash[] = {
	0x58, 0x5E, 0x95, 0x75, 0xA8, 0x70, 0xDB, 0x3F, 0x7D, 0xE0,
	0xE9, 0xD3, 0xC7, 0x4F, 0xC9, 0xD7, 0xE7, 0xF0, 0x84, 0xCD
};

static constexpr const PatchDefinition *DiscordStable00403KrispAllowedPatches[] = {
	&MklServIntelCpuTrueOneApiBuild20201104X8664
};

static constexpr ImageVariant DiscordStable00403KrispX8664 {
	"discord-stable-0.0.403-krisp-x86_64-585e9575",
	&DiscordStableKrispApplication,
	"0.0.403",
	"x86_64",
	DiscordStable00403KrispCdHash,
	sizeof(DiscordStable00403KrispCdHash),
	MatchMode::StrictVariant,
	0x650100,
	0x4D00,
	0xCBCED0,
	DiscordStable00403KrispAllowedPatches,
	sizeof(DiscordStable00403KrispAllowedPatches) /
		sizeof(DiscordStable00403KrispAllowedPatches[0]),
	"Initial Discord Stable/Krisp controlled-test fixture; functional runtime test pending"
};

static constexpr const ApplicationRule *BuiltInApplicationRules[] = {
	&DiscordStableKrispApplication
};

static constexpr const ImageVariant *BuiltInImageVariants[] = {
	&DiscordStable00403KrispX8664
};

static constexpr size_t BuiltInApplicationRuleCount =
	sizeof(BuiltInApplicationRules) / sizeof(BuiltInApplicationRules[0]);
static constexpr size_t BuiltInImageVariantCount =
	sizeof(BuiltInImageVariants) / sizeof(BuiltInImageVariants[0]);

static_assert(sizeof(MklServIntelCpuTrueReturnTrueX8664) <=
	sizeof(MklServIntelCpuTrueOneApiBuild20201104Search),
	"replacement must fit in the verified target");
static_assert(sizeof(DiscordStable00403KrispCdHash) == 20,
	"CodeDirectory hashes must use XNU's 20-byte CDHash representation");
static_assert(DiscordStable00403KrispX8664.targetFileOffset >=
	DiscordStable00403KrispX8664.executableTextStart,
	"target must start inside __TEXT,__text");
static_assert(DiscordStable00403KrispX8664.targetFileOffset +
	sizeof(MklServIntelCpuTrueOneApiBuild20201104Search) <=
	DiscordStable00403KrispX8664.executableTextEnd,
	"target must end inside __TEXT,__text");
static_assert((DiscordStable00403KrispX8664.targetFileOffset &
	(InitialX8664ValidationRangeSize - 1)) >=
	sizeof(MklServIntelCpuTrueOneApiBuild20201104ContextBefore),
	"strict target before-context must fit in one validation range");
static_assert((DiscordStable00403KrispX8664.targetFileOffset &
	(InitialX8664ValidationRangeSize - 1)) +
	sizeof(MklServIntelCpuTrueOneApiBuild20201104Search) +
	sizeof(MklServIntelCpuTrueOneApiBuild20201104ContextAfter) <=
	InitialX8664ValidationRangeSize,
	"strict target and context must fit in one validation range");

} // namespace IMKLFX

#endif /* IntelMKLFixupCatalogue_hpp */
