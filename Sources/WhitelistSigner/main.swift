import Foundation
import WhitelistCore

@main
struct WhitelistSignerCommand {
	static func main() {
		do {
			try run(Array(CommandLine.arguments.dropFirst()))
		} catch {
			let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
			FileHandle.standardError.write(Data("imklfx-whitelist-sign: error: \(message)\n".utf8))
			exit(1)
		}
	}

	static func run(_ arguments: [String]) throws {
		if arguments.first == "generate-key" {
			try generateKey(Array(arguments.dropFirst()))
			return
		}
		guard arguments.count == 4,
			arguments[0] == "--manifest", arguments[2] == "--output" else {
			throw WhitelistError.usage(
				"usage: imklfx-whitelist-sign --manifest FILE --output FILE"
			)
		}
		let environment = ProcessInfo.processInfo.environment
		guard let privateKeyText = environment["IMKLFX_SIGNING_PRIVATE_KEY_BASE64"],
			let privateKey = Data(base64Encoded: privateKeyText),
			let keyID = environment["IMKLFX_SIGNING_KEY_ID"], !keyID.isEmpty else {
			throw WhitelistError.invalidSignature(
				"IMKLFX_SIGNING_PRIVATE_KEY_BASE64 and IMKLFX_SIGNING_KEY_ID are required"
			)
		}

		let manifestURL = URL(fileURLWithPath: arguments[1])
		let outputURL = URL(fileURLWithPath: arguments[3])
		let manifestData = try Data(contentsOf: manifestURL)
		let pluginVersion = try SemanticVersion("1.0.0")
		_ = try ManifestValidator().validate(manifestData, pluginVersion: pluginVersion)

		let configured = try ReleaseTrust.trustRoot()
		guard configured.keyID == keyID else {
			throw WhitelistError.invalidSignature("secret key id does not match the embedded release key id")
		}
		let signer = ManifestSigner()
		guard try signer.publicKey(for: privateKey) == configured.publicKey else {
			throw WhitelistError.invalidSignature("private key does not match the embedded release public key")
		}
		let signature = try signer.sign(manifestData: manifestData, privateKey: privateKey, keyID: keyID)
		try signature.write(to: outputURL, options: [.atomic])
		print("signed manifest with key id \(keyID); private key material was not written")
	}

	static func generateKey(_ arguments: [String]) throws {
		guard arguments.count == 4,
			arguments[0] == "--key-id", arguments[2] == "--private-output" else {
			throw WhitelistError.usage(
				"usage: imklfx-whitelist-sign generate-key --key-id ID --private-output FILE"
			)
		}
		let keyID = arguments[1]
		guard keyID.range(of: "^[a-z0-9][a-z0-9._-]{0,63}$", options: .regularExpression) != nil else {
			throw WhitelistError.invalidSignature("key id is malformed")
		}
		let output = URL(fileURLWithPath: arguments[3])
		let pair = ManifestSigner().generateKeyPair()
		var privateText = Data(pair.privateKey.base64EncodedString().utf8)
		privateText.append(0x0A)
		guard !FileManager.default.fileExists(atPath: output.path) else {
			throw WhitelistError.invalidSignature("refusing to overwrite the private-key file")
		}
		do {
			try privateText.write(to: output, options: [.withoutOverwriting])
			try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: output.path)
		} catch {
			throw WhitelistError.invalidSignature("could not create the private-key file: \(error.localizedDescription)")
		}
		print("key id: \(keyID)")
		print("public key base64: \(pair.publicKey.base64EncodedString())")
		print("private key seed written with mode 0600 to: \(output.path)")
		print("Do not commit the private-key file.")
	}
}
