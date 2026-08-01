#include "../../IntelMKLFixup/IntelMKLFixupCatalogue.hpp"

#include <assert.h>

int main() {
	constexpr char Accepted[] =
		"/Users/test/Library/Application Support/discord/app-0.0.403/modules/"
		"discord_krisp-1/discord_krisp/discord_krisp.node";
	constexpr char RejectedChannel[] =
		"/Users/test/Library/Application Support/discordcanary/app-0.0.403/modules/"
		"discord_krisp-1/discord_krisp/discord_krisp.node";
	constexpr char RejectedBasename[] =
		"/Users/test/Library/Application Support/discord/app-0.0.403/modules/"
		"discord_krisp-1/discord_krisp/other.node";

	assert(IMKLFX::matchApplicationRule(IMKLFX::DiscordStableKrispApplication,
		Accepted, sizeof(Accepted) - 1));
	assert(!IMKLFX::matchApplicationRule(IMKLFX::DiscordStableKrispApplication,
		RejectedChannel, sizeof(RejectedChannel) - 1));
	assert(!IMKLFX::matchApplicationRule(IMKLFX::DiscordStableKrispApplication,
		RejectedBasename, sizeof(RejectedBasename) - 1));
	assert(IMKLFX::DiscordStable00403KrispX8664.allowedPatches[0] ==
		&IMKLFX::MklServIntelCpuTrueOneApiBuild20201104X8664);
	return 0;
}
