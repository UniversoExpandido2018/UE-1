/*
 * AccountManager.cpp
 *
 *  Created on: Oct 13, 2010
 *      Author: crush
 */

#include "server/login/account/Account.h"
#include "AccountManager.h"
#include "server/login/LoginClient.h"
#include "server/login/LoginServer.h"
#ifdef WITH_SESSION_API
#include "server/login/SessionAPIClient.h"
#endif // WITH_SESSION_API
#include "server/login/packets/AccountVersionMessage.h"
#include "server/login/packets/EnumerateCharacterId.h"
#include "server/login/packets/LoginClientToken.h"
#include "server/login/packets/LoginClusterStatus.h"
#include "server/login/packets/LoginEnumCluster.h"
#include "server/ServerCore.h"

#include "server/zone/managers/object/ObjectManager.h"

ReadWriteLock AccountManager::mutex;

AccountManager::AccountManager(LoginServer* loginserv) : Logger("AccountManager") {
	loginServer = loginserv;

	autoRegistration = true;
	requiredVersion = "";
	maxOnlineCharacters = 1;

	setLogging(false);
	setGlobalLogging(false);

	if (ServerCore::truncateDatabases()) {
		try {
			String query = "TRUNCATE TABLE characters";

			UniqueReference<ResultSet*> res(ServerDatabase::instance()->executeQuery(query));

			info("characters table truncated", true);
		} catch (const Exception& e) {
			error(e.getMessage());
		}
	}
}

AccountManager::~AccountManager() {

}

void AccountManager::loginAccount(LoginClient* client, Message* packet) {
	String username, password, version;
	AccountVersionMessage::parse(packet, username, password, version);

	Database::escapeString(username);
	Database::escapeString(password);

	if (!isRequiredVersion(version)) {
		client->sendErrorMessage("Login Error", "The client you are attempting to connect with does not match that required by the server.");
		return;
	}

	Reference<Account*> account = validateAccountCredentials(client, username, password);

	if (account == nullptr)
		return;

#ifdef WITH_SESSION_API
	SessionAPIClient::instance()->approveNewSession(client->getIPAddress(), account->getAccountID(),
			[this,
			loginClient = Reference<LoginClient*>(client),
			loginAccount = Reference<Account*>(account)
			](SessionApprovalResult result) {

		if (result.isActionTemporaryFailure()) {
			error()
			    << "Unexpected failure in approveNewSession for user ["
				<< (loginAccount == nullptr ? "<unknown user>" : loginAccount->getUsername())
				<< "]: " << result.getLogMessage();
		}

		if (loginClient == nullptr || loginAccount == nullptr)
			return;

		if (!result.isActionAllowed()) {
			loginClient->sendErrorMessage(result.getTitle(), result.getMessage(true));
			return;
		}

		loginApprovedAccount(loginClient, loginAccount);
	});
};

void AccountManager::loginApprovedAccount(LoginClient* client, ManagedReference<Account*> account) {
#endif // WITH_SESSION_API
	String sessionID = Crypto::randomSalt<64>();

	client->debug() << "generated sessionID: " << sessionID;

	Message* lct = new LoginClientToken(account, sessionID);
	client->sendMessage(lct);

	uint32 accountID = account->getAccountID();

	client->setAccountID(accountID);

	String ip = client->getSession()->getAddress().getIPAddress();

#ifdef WITH_SESSION_API
	SessionAPIClient::instance()->notifySessionStart(ip, accountID);
#endif // WITH_SESSION_API

	StringBuffer sessionQuery;
	sessionQuery << "REPLACE INTO sessions (account_id, session_id, ip, expires) VALUES (";
	sessionQuery << accountID << ", '" << sessionID << "', '" << ip << "' , ADDTIME(NOW(), '00:15'));";

	StringBuffer logQuery;
	logQuery << "INSERT INTO account_log (account_id, ip_address, timestamp) VALUES (" << accountID << ", '" << ip << "', NOW());";

	try {
		ServerDatabase::instance()->executeStatement(sessionQuery);
		ServerDatabase::instance()->executeStatement(logQuery);
	} catch (const DatabaseException& e) {
		client->error() << e.getMessage();
	}

	client->sendMessage(loginServer->getLoginEnumClusterMessage(account));
	client->sendMessage(loginServer->getLoginClusterStatusMessage(account));

	Message* eci = new EnumerateCharacterID(account);
	client->sendMessage(eci);
}

Reference<Account*> AccountManager::validateAccountCredentials(LoginClient* client, const String& username, const String& password) {
	StringBuffer query;
	query << "SELECT a.account_id, a.username, a.password, a.salt, a.account_id, a.station_id, "
		"UNIX_TIMESTAMP(a.created), a.admin_level FROM accounts a WHERE a.username = '" << username << "' LIMIT 1;";

	String passwordStored;
	Reference<Account*> account = getAccount(query.toString(), passwordStored, true); //force update of mysql rows to update galaxy bans

	if (account == nullptr) {
		//The user name didn't exist, so we check if auto registration is enabled and create a new account
		if (isAutoRegistrationEnabled() && client != nullptr) {
			account = createAccount(username, password, passwordStored);
		} else {
			if (client != nullptr) {
				client->sendErrorMessage("Login Error",
					ConfigManager::instance()->getString("Core3.RegistrationMessage",
						"El Registro Automatico esta desactivado. "
						"Por favor, contacta con un Administrador para conseguir acceso."
					)
				);
			}

			return nullptr;
		}
	}

	if (!account->isActive()) {
		if (client != nullptr) {
			const String& inactTitle = ConfigManager::instance()->getInactiveAccountTitle();
			const String& inactText = ConfigManager::instance()->getInactiveAccountText();

			client->sendErrorMessage(
				inactTitle.length() == 0 ? "Cuenta desactivada" : inactTitle,
				inactText.length() == 0 ? "Contacta con un Administrador para saber que ha sucedido." : inactText
			);
		}

		return nullptr;
	}

	//Check hash version
	String passwordHashed;
	if (account->getSalt() == "") {
		passwordHashed = Crypto::SHA1Hash(password);
	} else {
		passwordHashed = Crypto::SHA256Hash(dbSecret + password + account->getSalt());
	}

	if (passwordStored != passwordHashed) {
		if(client != nullptr)
			client->sendErrorMessage("Clave Erronea", " La clave que has insertado no es correcta.");

		return nullptr;
	}
	//update hash if unsalted
	if (account->getSalt() == "")
		updateHash(username, password);

	//Check if they are banned
	if (account->isBanned()) {
		StringBuffer reason;
		reason << "Tu cuenta ha sido baneada por un Administrador.\n\n";
		int totalBan = account->getBanExpires() - time(0);

		int daysBanned = floor((float)totalBan / 60.f / 60.f / 24.f);
		totalBan -= (daysBanned * 60 * 60 * 24);

		int hoursBanned = floor(((float)totalBan / 60.0f) / 60.f);
		totalBan -= (hoursBanned * 60 * 60);

		int minutesBanned = floor((float)totalBan / 60.0f);
		totalBan -= (minutesBanned * 60);

		reason << "Time remaining: ";

		if (daysBanned > 0)
			reason << daysBanned << " Days ";

		if (hoursBanned > 0)
			reason << hoursBanned << " Hours ";

		if (minutesBanned > 0)
			reason << minutesBanned << " Minutes ";

		reason << totalBan << " Seconds\n";

		reason << "Reason: " << account->getBanReason();

		if (client != nullptr)
			client->sendErrorMessage("Cuenta baneada", reason.toString());

		return nullptr;
	}

	return account;
}

void AccountManager::updateHash(const String& username, const String& password) {
	String salt = Crypto::randomSalt();
	String hash = Crypto::SHA256Hash(dbSecret + password + salt);

	StringBuffer query;
	query << "UPDATE accounts SET password = '" << hash << "', ";
	query << "salt = '" << salt << "' ";
	query << "WHERE username = '" << username << "';";

	try {
		ServerDatabase::instance()->executeStatement(query);
	} catch (const DatabaseException& e) {
		error(e.getMessage());
	}
}

Reference<Account*> AccountManager::createAccount(const String& username, const String& password, String& passwordStored) {
	uint32 stationID = System::random();

	String salt = Crypto::randomSalt();
	String hash = Crypto::SHA256Hash(dbSecret + password + salt);

	StringBuffer query;
	query << "INSERT INTO accounts (username, password, station_id, salt) VALUES (";
	query << "'" << username << "',";
	query << "'" << hash << "',";
	query << stationID << ",";
	query << "'" << salt << "');";

	UniqueReference<ResultSet*> result(ServerDatabase::instance()->executeQuery(query.toString()));

	if (result == nullptr)
		return nullptr;

	uint32 accountID = result->getLastAffectedRow();

	return getAccount(accountID, passwordStored, true);
}

Reference<Account*> AccountManager::getAccount(uint32 accountID, bool forceSqlUpdate) {
	static Logger logger("AccountManager");

	Locker locker(&mutex);

	Reference<Account*> accObj;

	static uint64 databaseID = ObjectDatabaseManager::instance()->getDatabaseID("accounts");

	uint64 oid = (accountID | (databaseID << 48));

	accObj = Core::getObjectBroker()->lookUp(oid).castTo<Account*>();

	if (accObj == nullptr) {
		// Lazily create account object
		accObj = dynamic_cast<Account*>(ObjectManager::instance()->createObject("Account", 3, "accounts", oid));

		if (accObj == nullptr) {
			logger.error("Error creating account object with account ID " + String::hexvalueOf((int64)oid));

			return nullptr;
		}
	} else if (!forceSqlUpdate && accObj->isSqlLoaded()) {
		return accObj;
	}

	StringBuffer query;
	query << "SELECT a.active, a.username, a.password, a.salt, a.account_id, a.station_id, UNIX_TIMESTAMP(a.created), a.admin_level FROM accounts a WHERE a.account_id = '" << accountID << "' LIMIT 1;";

	UniqueReference<ResultSet*> result(ServerDatabase::instance()->executeQuery(query.toString()));

	if (result->next()) {
		Locker locker(accObj);

		accObj->setActive(result->getBoolean(0));
		accObj->setUsername(result->getString(1));
		accObj->setSalt(result->getString(3));
		accObj->setAccountID(accountID);
		accObj->setStationID(result->getUnsignedInt(5));
		accObj->setTimeCreated(result->getUnsignedInt(6));
		accObj->setAdminLevel(result->getInt(7));

		accObj->updateFromDatabase();

		return accObj;
	}

	return nullptr;
}

Reference<Account*> AccountManager::getAccount(uint32 accountID, String& passwordStored, bool forceSqlUpdate) {
	StringBuffer query;
	query << "SELECT a.active, a.username, a.password, a.salt, a.account_id, a.station_id, UNIX_TIMESTAMP(a.created), a.admin_level FROM accounts a WHERE a.account_id = '" << accountID << "' LIMIT 1;";

	return getAccount(query.toString(), passwordStored, forceSqlUpdate);
}

Reference<Account*> AccountManager::getAccount(String query, String& passwordStored, bool forceSqlUpdate) {
	static Logger logger("AccountManager");

	Locker locker(&mutex);

	Reference<Account*> account;

	UniqueReference<ResultSet*> result(ServerDatabase::instance()->executeQuery(query));

	if (result->next()) {
		static uint64 databaseID = ObjectDatabaseManager::instance()->getDatabaseID("accounts");
		uint64 accountID = result->getUnsignedInt(4);

		uint64 oid = (accountID | (databaseID << 48));

		account = Core::getObjectBroker()->lookUp(oid).castTo<Account*>();

		if (account == nullptr) {
			// Lazily create account object
			account = dynamic_cast<Account*>(ObjectManager::instance()->createObject("Account", 3, "accounts", oid));

			if (account == nullptr) {
				logger.error("Error creating account object with account ID " + String::hexvalueOf((int64)oid));

				return nullptr;
			}
		} else if (!forceSqlUpdate && account->isSqlLoaded()) {
			return account;
		}

		if (account == nullptr) {
			return nullptr;
		}

		Locker locker(account);

		account->setActive(result->getBoolean(0));
		account->setUsername(result->getString(1));

		passwordStored = result->getString(2);

		account->setSalt(result->getString(3));
		account->setAccountID(accountID);
		account->setStationID(result->getUnsignedInt(5));
		account->setTimeCreated(result->getUnsignedInt(6));
		account->setAdminLevel(result->getInt(7));

		account->updateFromDatabase();

		return account;
	}

	return nullptr;
}

Reference<Account*> AccountManager::getAccount(const String& accountName, bool forceSqlUpdate) {
	String name = accountName;

	Database::escapeString(name);

	StringBuffer query;
	query << "SELECT a.active, a.username, a.password, a.salt, a.account_id, a.station_id, UNIX_TIMESTAMP(a.created), a.admin_level FROM accounts a WHERE a.username = '" << name << "' LIMIT 1;";

	String temp;

	return getAccount(query.toString(), temp, forceSqlUpdate);
}
