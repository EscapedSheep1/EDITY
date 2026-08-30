#include "app/Utf8.h"
#include "app/Util.h"
#include "market/JsonIO.h"
#include "market/Validator.h"
#include "types/TypesCatalog.h"
#include "types/TypesXmlIO.h"
#include "loadout/LoadoutIO.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace edity;

namespace {

std::string Load(const std::filesystem::path& dir, const char* name) {
    return ReadFileUtf8(dir / name);
}

int Fail(const std::string& message) {
    std::cerr << "FAIL: " << message << "\n";
    return 1;
}

bool HasError(const std::vector<ValidationIssue>& issues, const std::string& needle) {
    for (const auto& issue : issues) {
        if (issue.severity == ValidationIssue::Severity::Error && issue.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool HasWarning(const std::vector<ValidationIssue>& issues, const std::string& needle) {
    for (const auto& issue : issues) {
        if (issue.severity == ValidationIssue::Severity::Warning && issue.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    const std::filesystem::path fixtures = EDITY_TEST_FIXTURES;
    int failed = 0;

    const auto rifles = ParseMarket("Assault_Rifles.json", Load(fixtures, "Assault_Rifles.json"));
    const auto pistols = ParseMarket("Pistols.json", Load(fixtures, "Pistols.json"));
    const auto dup = ParseMarket("Duplicate_Pistols.json", Load(fixtures, "Duplicate_Pistols.json"));
    const auto trader = ParseTrader("Weapons.json", Load(fixtures, "Weapons.json"));
    const auto zone = ParseZone("GreenMountain.json", Load(fixtures, "GreenMountain.json"));
    const auto badZone = ParseZone("BadZone.json", Load(fixtures, "BadZone.json"));
    const auto broken = ParseMarket("Broken.json", "{ not json");

    if (!rifles.ok || !pistols.ok || !dup.ok || !trader.ok || !zone.ok || !badZone.ok) {
        return Fail("wiki-shaped fixtures should parse");
    }
    if (broken.ok) {
        return Fail("broken JSON must not parse");
    }

    WorkspaceSnapshot clean;
    clean.markets = {rifles.market, pistols.market};
    clean.traders = {trader.trader};
    clean.traders[0].categories.erase(clean.traders[0].categories.begin() + 1);
    clean.zones = {zone.zone};
    const auto cleanIssues = ValidateWorkspace(clean);
    if (HasError(cleanIssues, "already exists") || HasError(cleanIssues, "does not match")) {
        return Fail("clean workspace should not have uniqueness or missing-category errors");
    }
    if (!HasWarning(cleanIssues, "not present in any Market file")) {
        ++failed;
        Fail("orphan zone stock should warn");
    }

    WorkspaceSnapshot dups;
    dups.markets = {pistols.market, dup.market};
    const auto dupIssues = ValidateWorkspace(dups);
    if (!HasError(dupIssues, "already exists")) {
        ++failed;
        Fail("case-insensitive duplicate classname across files should error");
    }

    WorkspaceSnapshot attSnap;
    attSnap.markets = {rifles.market};
    const auto attIssues = ValidateWorkspace(attSnap);
    if (!HasError(attIssues, "not a ClassName or Variant in any Market file")) {
        ++failed;
        Fail("attachment missing from all market files should error");
    }
    auto riflesOk = rifles.market;
    riflesOk.items[0].spawnAttachments = {"fnx45"};
    WorkspaceSnapshot attOk;
    attOk.markets = {riflesOk, pistols.market};
    const auto attOkIssues = ValidateWorkspace(attOk);
    if (HasError(attOkIssues, "not a ClassName or Variant in any Market file")) {
        ++failed;
        Fail("attachment that exists as a market classname should pass");
    }

    WorkspaceSnapshot missing;
    missing.markets = {rifles.market};
    missing.traders = {trader.trader};
    const auto missingIssues = ValidateWorkspace(missing);
    if (!HasError(missingIssues, "does not match any Market filename")) {
        ++failed;
        Fail("unknown trader category should error");
    }

    WorkspaceSnapshot zoneSnap;
    zoneSnap.zones = {badZone.zone};
    const auto zoneIssues = ValidateWorkspace(zoneSnap);
    if (!HasError(zoneIssues, "m_DisplayName is required") || !HasError(zoneIssues, "Radius must be greater than 0")) {
        ++failed;
        Fail("bad zone should fail display name and radius");
    }

    const auto raw = Load(fixtures, "Weapons.json");
    if (raw.find("akm") == std::string::npos) {
        ++failed;
        Fail("fixture read failed");
    }

    const auto modern = ParseTrader("Pulkovo_Weapons.json", Load(fixtures, "Pulkovo_Weapons.json"));
    if (!modern.ok || !modern.trader.traderName.empty() || modern.trader.version != 13) {
        ++failed;
        Fail("v13 trader without TraderName must parse and keep the name empty");
    }
    if (modern.trader.minRequiredReputation != 0 || modern.trader.displayCurrencyValue != 1 ||
        modern.trader.requiredCompletedQuestId != -1) {
        ++failed;
        Fail("v13 reputation and currency fields must be preserved");
    }
    WorkspaceSnapshot modernSnap;
    modernSnap.markets = {rifles.market, pistols.market};
    modernSnap.traders = {modern.trader};
    const auto modernIssues = ValidateWorkspace(modernSnap);
    if (HasError(modernIssues, "TraderName is required")) {
        ++failed;
        Fail("v13 traders must not require TraderName");
    }
    const auto written = SerializeTrader(modern.trader);
    if (written.find("\"TraderName\"") != std::string::npos) {
        ++failed;
        Fail("empty TraderName must not be written back onto v13 files");
    }
    if (written.find("FutureProofKey") == std::string::npos || written.find("keep-me") == std::string::npos) {
        ++failed;
        Fail("unknown trader keys must survive a save");
    }
    if (written.find("MinRequiredReputation") == std::string::npos) {
        ++failed;
        Fail("v13 fields must be written on save");
    }

    const auto types = ParseTypesXml(Load(fixtures, "types.xml"), "types.xml");
    if (types.size() != 3 || types[0].name != "AKM" || types[0].category != "weapons" ||
        types[2].name != "WoodenCrate") {
        ++failed;
        Fail("types.xml classnames and categories must parse");
    }
    const auto econ = ParseEconomyCore(Load(fixtures, "cfgeconomycore.xml"));
    if (econ.size() != 2 || econ[0].Relative() != "db/types.xml" ||
        econ[1].Relative() != "CustomTypes/Carpack_types.xml") {
        ++failed;
        Fail("cfgeconomycore.xml must list only active type=\"types\" files");
    }
    auto withVanilla = ParseEconomyCore("<economycore><ce folder=\"CustomTypes\"><file name=\"mod_types.xml\" type=\"types\" /></ce></economycore>");
    EnsureVanillaTypesFile(withVanilla);
    if (withVanilla.size() != 2 || withVanilla[0].Relative() != "db/types.xml" ||
        withVanilla[1].Relative() != "CustomTypes/mod_types.xml") {
        ++failed;
        Fail("vanilla db/types.xml must be added when cfgeconomycore omits it");
    }
    auto alreadyListed = econ;
    EnsureVanillaTypesFile(alreadyListed);
    if (alreadyListed.size() != 2) {
        ++failed;
        Fail("vanilla db/types.xml must not be added twice when already listed");
    }
    if (GuessMissionRoot("mpmissions/dayzOffline.chernarusplus/expansion/traderzones") !=
        "mpmissions/dayzOffline.chernarusplus") {
        ++failed;
        Fail("mission root should be derived from the TraderZones path");
    }

    const auto fullTypes = ParseTypesDocument(Load(fixtures, "types.xml"), "db/types.xml");
    if (!fullTypes.ok || fullTypes.doc.types.size() != 3 || fullTypes.doc.types[0].name != "AKM" ||
        fullTypes.doc.types[0].nominal != 10 || fullTypes.doc.types[0].min != 5 ||
        fullTypes.doc.types[0].flags.countInMap != 1 || fullTypes.doc.types[0].usages.size() != 1 ||
        fullTypes.doc.types[0].usages[0] != "Military") {
        ++failed;
        Fail("types.xml must parse CE fields, flags, usage, and value");
    }
    const auto roundTrip = ParseTypesDocument(SerializeTypesDocument(fullTypes.doc), "db/types.xml");
    if (!roundTrip.ok || roundTrip.doc.types.size() != 3 || roundTrip.doc.types[0].lifetime != 14400 ||
        roundTrip.doc.types[0].values[0] != "Tier3") {
        ++failed;
        Fail("types.xml must survive a serialize/parse round trip");
    }
    if (TypesFileName("CustomTypes/My Mod") != "CustomTypes/My_Mod_types.xml" ||
        TypesFileName("db/types.xml") != "db/types.xml") {
        ++failed;
        Fail("TypesFileName must keep relative xml paths");
    }
    auto econXml = Load(fixtures, "cfgeconomycore.xml");
    EconomyCoreAddFile(econXml, "CustomTypes/Extra_types.xml");
    if (!EconomyCoreHasFile(econXml, "CustomTypes/Extra_types.xml") ||
        !EconomyCoreHasFile(econXml, "db/types.xml")) {
        ++failed;
        Fail("new types files must register in cfgeconomycore.xml");
    }
    EconomyCoreRemoveFile(econXml, "CustomTypes/Carpack_types.xml");
    if (EconomyCoreHasFile(econXml, "CustomTypes/Carpack_types.xml")) {
        ++failed;
        Fail("deleted types files must leave cfgeconomycore.xml");
    }

    WorkspaceSnapshot typeSnap;
    TypesDocument badDoc;
    badDoc.relPath = "db/types.xml";
    TypeDefinition bad = DefaultType("");
    badDoc.types.push_back(bad);
    TypeDefinition minBad = DefaultType("Barrel");
    minBad.nominal = 4;
    minBad.min = 8;
    badDoc.types.push_back(minBad);
    typeSnap.typesFiles.push_back(badDoc);
    const auto typeIssues = ValidateWorkspace(typeSnap);
    if (!HasError(typeIssues, "Type classname cannot be empty") ||
        !HasError(typeIssues, "Min must be less than or equal to nominal")) {
        ++failed;
        Fail("types lint must catch empty names and min > nominal");
    }
    WorkspaceSnapshot zombieSnap;
    TypesDocument zombieDoc;
    zombieDoc.relPath = "db/types.xml";
    TypeDefinition zombieF = DefaultType("ZmbF_JournalistNormal_Blue");
    zombieF.nominal = 0;
    zombieF.min = 2;
    zombieDoc.types.push_back(zombieF);
    TypeDefinition zombieM = DefaultType("ZmbM_CitizenASkinny_Blue");
    zombieM.nominal = 0;
    zombieM.min = 2;
    zombieDoc.types.push_back(zombieM);
    zombieSnap.typesFiles.push_back(zombieDoc);
    if (HasError(ValidateWorkspace(zombieSnap), "Min must be less than or equal to nominal")) {
        ++failed;
        Fail("ZmbF and ZmbM types must be skipped by types lint");
    }

    if (JsonFileName("Carpack_Parts.json") != "Carpack_Parts.json" ||
        JsonFileName("Carpack_Parts") != "Carpack_Parts.json" ||
        JsonFileName("New Category") != "New_Category.json") {
        ++failed;
        Fail("JsonFileName must keep the .json extension on existing files");
    }

    const auto loadout = ParseLoadout("HumanLoadout.json", Load(fixtures, "HumanLoadout.json"));
    if (!loadout.ok || loadout.file.root.className != "SurvivorM_Jose" ||
        loadout.file.root.attachments.size() != 1 || loadout.file.root.attachments[0].second.size() != 2 ||
        loadout.file.root.attachments[0].second[0].className != "TacticalShirt_Black" ||
        loadout.file.root.cargo.size() != 1 || loadout.file.root.cargo[0].className != "Rag" ||
        loadout.file.root.cargo[0].quantityMin != 4.0 || loadout.file.root.sets.size() != 1 ||
        !loadout.file.root.sets[0].className.empty()) {
        ++failed;
        Fail("classic Expansion loadouts must parse Attachments, Inventory, and Sets");
    }
    const auto loadoutRound = ParseLoadout("HumanLoadout.json", SerializeLoadout(loadout.file));
    if (!loadoutRound.ok || loadoutRound.file.root.cargo[0].quantityMax != 4.0 ||
        loadoutRound.file.root.sets[0].chance != 0.5 ||
        SerializeLoadout(loadout.file).find("InventoryAttachments") == std::string::npos) {
        ++failed;
        Fail("loadouts must serialize the modern Expansion tree and round-trip");
    }
    const auto loadoutUi = LoadoutFromUi(LoadoutToUi(loadout.file));
    if (loadoutUi.root.className != "SurvivorM_Jose" || loadoutUi.root.cargo.size() != 1) {
        ++failed;
        Fail("loadout UI mapping must keep the tree");
    }
    const auto spawnSettings = ParseLoadout("SpawnSettings.json", R"({"StartingGear":[],"StartingClothing":[]})");
    if (spawnSettings.ok) {
        ++failed;
        Fail("SpawnSettings.json must not be accepted as a loadout");
    }

    WorkspaceSnapshot loadoutSnap;
    loadoutSnap.loadouts = {loadout.file};
    TypesDocument typeDoc;
    typeDoc.relPath = "db/types.xml";
    typeDoc.types.push_back(DefaultType("Rag"));
    typeDoc.types.push_back(DefaultType("AKM"));
    loadoutSnap.typesFiles.push_back(typeDoc);
    const auto loadoutIssues = ValidateWorkspace(loadoutSnap);
    if (!HasWarning(loadoutIssues, "not in the pulled types catalog")) {
        ++failed;
        Fail("unknown loadout classnames should warn against the types catalog");
    }
    if (HasError(loadoutIssues, "ClassName or Include")) {
        ++failed;
        Fail("empty ClassName on a Set must be allowed");
    }

    LoadoutFile badChance = DefaultLoadout("BadChance.json");
    badChance.root.chance = 2.0;
    WorkspaceSnapshot chanceSnap;
    chanceSnap.loadouts = {badChance};
    if (!HasError(ValidateWorkspace(chanceSnap), "Chance must be between 0 and 1")) {
        ++failed;
        Fail("loadout Chance outside 0-1 must be an error");
    }

    LoadoutFile missingInclude = DefaultLoadout("HasInclude.json");
    missingInclude.root.includeFile = "PoliceLoadout.json";
    WorkspaceSnapshot includeSnap;
    includeSnap.loadouts = {missingInclude};
    if (!HasWarning(ValidateWorkspace(includeSnap), "Include file is not in the Loadouts folder")) {
        ++failed;
        Fail("missing Include target should warn");
    }

    if (failed == 0) {
        std::cout << "All validator tests passed\n";
        return 0;
    }
    std::cerr << failed << " test(s) failed\n";
    return 1;
}
