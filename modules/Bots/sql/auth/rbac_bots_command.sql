-- Bots module RBAC: the `.bot` chat command permission.
--
-- The core's own permission ids top out at 2009 (RBAC.h), so the module reserves
-- 2100 to match Bots::RBAC_PERM_BOTS_COMMAND in src/BotScripts.h. Import this
-- into the *auth* database. Every statement is idempotent (REPLACE / NOT EXISTS),
-- so re-running it is safe.
--
-- Security levels come from enum AccountTypes (SEC_ADMINISTRATOR = 3,
-- SEC_CONSOLE = 4); rbac_default_permissions grants a permission to everyone at
-- or above a security level, realmId = -1 meaning "all realms".

-- 1. Define the permission.
REPLACE INTO `rbac_permissions` (`id`, `name`) VALUES
(2100, 'Command: bot');

-- 2. Grant it to Administrators and Console.
INSERT INTO `rbac_default_permissions` (`secId`, `permissionId`, `realmId`)
SELECT 3, 2100, -1 FROM DUAL
WHERE NOT EXISTS (SELECT 1 FROM `rbac_default_permissions` WHERE `secId` = 3 AND `permissionId` = 2100 AND `realmId` = -1);

INSERT INTO `rbac_default_permissions` (`secId`, `permissionId`, `realmId`)
SELECT 4, 2100, -1 FROM DUAL
WHERE NOT EXISTS (SELECT 1 FROM `rbac_default_permissions` WHERE `secId` = 4 AND `permissionId` = 2100 AND `realmId` = -1);
