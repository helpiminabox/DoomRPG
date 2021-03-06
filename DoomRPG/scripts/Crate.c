#include "Defs.h"
#include "RPG.h"

#include "Crate.h"
#include "ItemData.h"
#include "Stats.h"
#include "Utils.h"

str const CrateRarityNames[MAX_DIFFICULTIES] =
{
    "\CcBasic",
    "\CjCommon",
    "\CdUncommon",
    "\CdSuperior",
    "\CnRare",
    "\CnVery Rare",
    "\CiElite",
    "\CtLegendary",
    "\CfSupreme"
};

RPGMap int CrateID = 0;
RPGMap CrateInfo Crates[CRATE_MAX];

NamedScript Type_OPEN void CrateLoop()
{
    while (true)
    {
        for (int i = 0; i < CrateID; i++)
        {
            // Hacking cooldown timer handling
            if (Crates[i].HackingCooldown > 0)
                Crates[i].HackingCooldown--;
        
            // Supply Drop spawned crates should never have a firewall
            if (Crates[i].SupplyDrop && Crates[i].Firewall)
            {
                Crates[i].Firewall = false;
                SetActorState(Crates[i].TID, "Normal");
            }
        }
        
        Delay(1);
    }
}

NamedScript DECORATE void InitCrate()
{
    int TID = UniqueTID();
    int Amount = 3;
    int LuckMod = (AveragePlayerLevel() + AveragePlayerLuck()) / 20;
    int Rarity = 0;
    int Firewall = 0;
    
    if (CrateID >= CRATE_MAX)
    {
        Log("\CgERROR:\C- Maximum Crate ID exceeded!");
        Thing_Remove(0);
        return;
    }
    
    // Calculate Rarity
    for (int i = Rarity; i < MAX_DIFFICULTIES - 1; i++)
        if (Random(0, (MAX_DIFFICULTIES + (MAX_DIFFICULTIES / 3)) - LuckMod) <= 0)
            Rarity++;
    if (Rarity < 0) // Make sure the Rarity still isn't -1, or else bad things will happen
        Rarity = 0;
    if (Rarity > MAX_DIFFICULTIES - 1)
        Rarity = MAX_DIFFICULTIES - 1;
    
    // Calculate Amount
    for (int i = Amount; i < CRATE_MAX_ITEMS; i++)
        if (Random(0, i) <= 0)
            Amount++;
    Amount += PlayerCount() - 1;      // Add +1 per extra player in-game
    if (Amount > CRATE_MAX_ITEMS - 1) // Make sure we don't overflow
        Amount = CRATE_MAX_ITEMS;
    
    // Calculate Firewall odds
    Firewall = Rarity * 10;
    if (Firewall <= 10)
        Firewall = 10;
    if (Firewall > 100)
        Firewall = 100;
    
    // Setup Crate
    Crates[CrateID].Firewall = (Random(0, 100) <= Firewall);
    Crates[CrateID].TID = TID;
    Crates[CrateID].Amount = Amount;
    Crates[CrateID].Rarity = Rarity;
    Crates[CrateID].Hacking = -1;
    Thing_ChangeTID(0, TID);
    SetUserVariable(0, "user_id", CrateID);
    SetUserVariable(0, "user_firewall", (int)Crates[CrateID].Firewall);
    if (Crates[CrateID].Firewall)
        SetActorState(0, "Firewall");
    else
        SetActorState(0, "Normal");
    
    for (int x = 0; x < CRATE_MAX_ITEMS; x++)
    {
        Crates[CrateID].Active[x] = false;
        Crates[CrateID].Item[x] = NULL;
    }
    
    CrateID++;
}

NamedScript DECORATE void UseCrate(int ID)
{
    // Hackity hacks so you can't open the crate yet if the rewards list is not done being generated
    if (!RewardsInit) return;
    
    SetActivator(GetActorProperty(0, APROP_TargetTID));
    
    // Hack for if you somehow activate a crate while you're hacking another one
    if (Player.CrateHacking) return;
    
    // Someone else is hacking the crate
    if (Crates[ID].Hacking >= 0)
    {
        if (Crates[ID].Hacking != PlayerNumber())
        {
            PrintError(StrParam("%tS\C- is already hacking an item in this crate", Crates[Player.CrateID].Hacking + 1));
            ActivatorSound("menu/error", 127);
        }
        
        return;
    }
    
    // Check if you're in a menu
    if (Player.InMenu || Player.InShop || Player.CrateOpen) return;
    
    // Check if the crate is empty
    if (Crates[ID].Generated && CrateEmpty(ID)) return;
    
    // Check if the crate is firewalled, if so, initiate hacking
    if (Crates[ID].Firewall)
    {
        if (Crates[ID].HackingCooldown > 0)
        {
            PrintError(StrParam("Security Lockdown Triggered\n\nYou must wait %S before reattemtping the hack", FormatTime(Crates[ID].HackingCooldown)));
            ActivatorSound("menu/error", 127);
        }
        else
        {
            Player.CrateID = ID;
            CrateHack();
        }
        return;
    }
    
    // Generate the crate contents
    if (!Crates[ID].Generated)
    {
        GenerateCrate(ID, Crates[ID].Amount);
        Crates[ID].Generated = true;
    }
    
    Player.CrateOpen = true;
    Player.CrateID = ID;
    Player.CrateIndex = 0;
    
    ActivatorSound("crate/open", 127);
    
    Delay(1);
    
    while (Player.CrateOpen)
    {
        SetHudSize(GetActivatorCVar("drpg_menu_width"), GetActivatorCVar("drpg_menu_height"), true);
        
        SetPlayerProperty(0, 1, PROP_TOTALLYFROZEN);
        
        DrawCrate();
        CrateInput();
        
        Delay(1);
    }
    
    SetPlayerProperty(0, 0, PROP_TOTALLYFROZEN);
}

NamedScriptSync bool GenerateCrateNodes(CrateInfo RPGMap *Crate)
{
    int StopCount = 4000;
    
    while (Crate->GenType < NODE_MAX)
    {
        bool Conflict = false;
        int Start = Random(0, 300 - 12);
        int End = 4 * Random(1, 2);
        
        // Make sure we don't generate over the max amount of allowed node types
        if (Crate->GenNodes[Crate->GenType] >= Crate->NodeMax[Crate->GenType])
        {
            Crate->GenType++;
            continue;
        }
        
        // Make sure there are no node conflicts
        for (int i = 0; i < MAX_NODES; i++)
            if (Crate->Nodes[i].Active && !Conflict)
                for (int j = 0; j < End; j++)
                    if (Start + j >= Crate->Nodes[i].Start && Start + j <= Crate->Nodes[i].Start + Crate->Nodes[i].End)
                    {
                        Conflict = true;
                        break;
                    }
        
        // Get new coordinates due to a conflict
        if (Conflict)
            continue;
        
        // Setup Node
        Crate->Nodes[Crate->GenTotal].Active = true;
        Crate->Nodes[Crate->GenTotal].Type = Crate->GenType;
        Crate->Nodes[Crate->GenTotal].Start = Start;
        Crate->Nodes[Crate->GenTotal].End = End;
        
        Crate->GenTotal++;
        Crate->GenNodes[Crate->GenType]++;
        
        if (StopCount-- == 0)
            return false;
    }
    
    Crate->HacksGenerated = true;
    return true;
}

NamedScript void CrateHack()
{
    CrateInfo RPGMap *Crate = &Crates[Player.CrateID];
    
    if (!Crate->HacksGenerated)
    {
        Crate->Tries = 3;
        
        Crate->NodeMax[0] = 8;
        Crate->NodeMax[1] = 8;
        Crate->NodeMax[2] = 1;
        Crate->NodeMax[3] = 2;
        Crate->NodeMax[4] = 2;
        Crate->NodeMax[5] = 1;
        Crate->NodeMax[6] = 1;
        Crate->NodeMax[7] = 1;

        // DRLA - Technician gets bonuses
        if (CompatMode == COMPAT_DRLA && PlayerClass(PlayerNumber()) == 2)
        {
            Crate->NodeMax[NODE_CRITFAIL] /= 4;
            Crate->NodeMax[NODE_EXPLODE] /= 4;
            Crate->NodeMax[NODE_XP] *= 2;
            Crate->NodeMax[NODE_RANK] *= 2;
            Crate->NodeMax[NODE_RETRY] *= 2;
            Crate->NodeMax[NODE_ADD] *= 2;
        }
        
        // Initialize things
        for (int i = 0; i < MAX_NODES; i++)
            Crate->Nodes[i].Active = false;
        for (int i = 0; i < NODE_MAX; i++)
            Crate->GenNodes[i] = 0;
        
        while (!GenerateCrateNodes(Crate));
    };
    
    bool Hacking = true;
    int X = Random(25, 275);
    int Direction = (Random(1, 2) == 1 ? -1 : 1);
    
    // Prevent the use input from leaking into the input handling below (aka exploding a crate immediately in your face)
    Delay(4);
    
    ActivatorSound("hacking/begin", 127);
    
    SetPlayerProperty(0, 1, PROP_TOTALLYFROZEN);
    Crates[Player.CrateID].Hacking = PlayerNumber();
    Player.CrateHacking = true;
    
    while (Hacking)
    {
        int Buttons = GetPlayerInput(PlayerNumber(), INPUT_BUTTONS);
        int OldButtons = GetPlayerInput(PlayerNumber(), INPUT_OLDBUTTONS);
        
        SetHudSize(320, 240, false);
        
        // HACKIFICATIONS
        SetFont("SMALLFONT");
        HudMessage("HACKING IN PROGRESS...");
        EndHudMessage(HUDMSG_PLAIN, 0, "Green", 160.0, 100.0, 0.05);
        if (!GetActivatorCVar("drpg_toaster"))
            for (int i = 0; i < 16; i++)
            {
                HudMessage("%d", Random(0, 1));
                EndHudMessage(HUDMSG_FADEOUT, 0, "Green", 10.0 + (8.0 * Random(1, 36)), 150.0 + (8.0 * Random(1, 10)), 0.25, 0.75);
            }
        
        // Cursor
        PrintSprite("HackCurs", 0, 10.1 + X, 120.0, 0.05);
        
        // Nodes
        for (int i = 0; i < MAX_NODES; i++)
            if (Crate->Nodes[i].Active)
                DrawBar(StrParam("HackNod%d", Crate->Nodes[i].Type + 1), 10 + Crate->Nodes[i].Start, 120, Crate->Nodes[i].End, true);
        
        // Back
        DrawBar(StrParam("HackBor%d", Crate->Tries), 10, 120, 300, true);
        PrintSprite("HackBack", 0, 10.1, 120.0, 0.05);
        
        // Move Cursor
        X += Direction;
        if ((Crates[Player.CrateID].Rarity / 3) > 0)
            X += ((Crates[Player.CrateID].Rarity / 3) * Direction);
        if (X < 0)
        {
            X = 0;
            Direction = -Direction;
        }
        else if (X > 300)
        {
            X = 300;
            Direction = -Direction;
        }
        
        // Input
        if (Buttons == BT_USE && OldButtons != BT_USE)
        {
            bool HitNothing = true;
            long int XPBonus = XPTable[Player.Level] / 100l;
            long int RankBonus = RankTable[Player.RankLevel] / 100l;
            
            // Check Nodes
            for (int i = 0; i < MAX_NODES; i++)
                if (Crate->Nodes[i].Active)
                    if (X >= Crate->Nodes[i].Start && X <= Crate->Nodes[i].Start + Crate->Nodes[i].End)
                    {
                        HitNothing = false;
                        Crate->Nodes[i].Active = false;
                        
                        switch (Crate->Nodes[i].Type)
                        {
                        case NODE_CRITFAIL:
                            ActivatorSound("hacking/critfail", 127);
                            SetFont("SMALLFONT");
                            HudMessage("Security Lockdown Triggered!");
                            EndHudMessage(HUDMSG_FADEOUT, 0, "Red", 160.0, 140.0, 2.0, 1.0);
                            Crates[Player.CrateID].HackingCooldown += 35 * 60;
                            Crate->Tries = 3;
                            Hacking = false;
                            break;
                        case NODE_EXPLODE:
                            ActivatorSound("hacking/critfail", 127);
                            SetFont("SMALLFONT");
                            HudMessage("Explosive Device Triggered!");
                            EndHudMessage(HUDMSG_FADEOUT, 0, "Orange", 160.0, 140.0, 2.0, 1.0);
                            SetActorState(Crates[Player.CrateID].TID, "Explode");
                            Hacking = false;
                            Player.CrateOpen = false;
                            break;
                        case NODE_UNLOCK:
                            ActivatorSound("hacking/success", 127);
                            SetFont("SMALLFONT");
                            HudMessage("Hack Successful");
                            EndHudMessage(HUDMSG_FADEOUT, 0, "Green", 160.0, 140.0, 2.0, 1.0);
                            Crates[Player.CrateID].Firewall = false;
                            SetActorState(Crates[Player.CrateID].TID, "Normal");
                            SetUserVariable(Crates[Player.CrateID].TID, "user_firewall", (int)Crates[CrateID].Firewall);
                            Hacking = false;
                            break;
                        case NODE_XP:
                            ActivatorSound("hacking/select", 127);
                            SetFont("SMALLFONT");
                            HudMessage("+%ld XP", XPBonus);
                            EndHudMessage(HUDMSG_FADEOUT, 0, "White", 160.0, 140.0, 2.0, 1.0);
                            Player.XP += XPBonus;
                            break;
                        case NODE_RANK:
                            ActivatorSound("hacking/select", 127);
                            SetFont("SMALLFONT");
                            HudMessage("+%ld Rank", RankBonus);
                            EndHudMessage(HUDMSG_FADEOUT, 0, "Yellow", 160.0, 140.0, 2.0, 1.0);
                            Player.Rank += RankBonus;
                            break;
                        case NODE_RETRY:
                            ActivatorSound("hacking/select", 127);
                            SetFont("SMALLFONT");
                            HudMessage("Extra Try");
                            EndHudMessage(HUDMSG_FADEOUT, 0, "Blue", 160.0, 140.0, 2.0, 1.0);
                            if (Crate->Tries < 3)
                                Crate->Tries++;
                            break;
                        case NODE_ADD:
                            ActivatorSound("hacking/select", 127);
                            SetFont("SMALLFONT");
                            HudMessage("Extra Item!");
                            EndHudMessage(HUDMSG_FADEOUT, 0, "Cyan", 160.0, 140.0, 2.0, 1.0);
                            if (Crates[Player.CrateID].Amount < CRATE_MAX_ITEMS - 1)
                                Crates[Player.CrateID].Amount++;
                            break;
                        case NODE_RARITY:
                            ActivatorSound("hacking/select", 127);
                            SetFont("SMALLFONT");
                            HudMessage("Rarity Upgrade!");
                            EndHudMessage(HUDMSG_FADEOUT, 0, "Purple", 160.0, 140.0, 2.0, 1.0);
                            if (Crates[Player.CrateID].Rarity < MAX_DIFFICULTIES - 1)
                                Crates[Player.CrateID].Rarity++;
                            break;
                        }
                        
                        break;
                    }
            
            if (HitNothing)
            {
                ActivatorSound("hacking/select", 127);
                Crate->Tries--;
                
                // You fail it
                if (Crate->Tries <= 0)
                {
                    ActivatorSound("hacking/fail", 127);
                    Crates[Player.CrateID].HackingCooldown += 35 * 30;
                    Crate->Tries = 3;
                    Hacking = false;
                }
            }
        }
        if (Buttons & BT_SPEED)
        {
            ActivatorSound("hacking/select", 127);
            Hacking = false;
        }
        
        Delay(1);
    }
    
    Delay(4);
    
    SetPlayerProperty(0, 0, PROP_TOTALLYFROZEN);
    Crates[Player.CrateID].Hacking = -1;
    Player.CrateHacking = false;
}

typedef struct _setinfo
{
    str Name;
    str Token;
    bool Synthfire;
    
    struct SetItemInfo
    {
        str Name;
        fixed Chance;
    } Items[32];
} SetInfo;
        
void GenerateCrate(int ID, int Amount)
{
    int i;
    int Firewalls;
    
    if (Crates[ID].SupplyDrop)
    {
        ItemInfoPtr Backpack = FindItem("DRPGBigBackpack");
        
        if (Backpack == GetBlankItem())
        {
            Log("\CgWARNING: Couldn't find the backpack item!");
        }
        
        int NumPlayers = PlayerCount();
        
        // Create a backpack for every player in the game
        for (int j = 0; j < NumPlayers; j++)
        {
            Crates[ID].Active[i] = true;
            Crates[ID].Item[i] = Backpack;
            i++;
        }
    }
    
    for (; i < Amount; i++)
    {
        ItemInfoPtr item = GetRewardItem(Crates[ID].Rarity);
        
        if (item == GetBlankItem())
        {
            i--;
            continue;
        }
        
        Crates[ID].Active[i] = true;
        Crates[ID].Item[i] = item;
    }
    
    // DRLA Set shenanigans
    if (CompatMode == COMPAT_DRLA)
    {
        SetInfo const SetItems[MAX_DRLA_SETS] =
        {
            {
                "Death From Above", "RLDeathFromAboveSetBonusActive", false,
                {
                    { "RLAntigravSteelBoots",               40, },
                    { "RLAntigravProtectiveBoots",          30, },
                    { "RLAntigravPlasteelBoots",            20, },
                    { "RLJetpackArmor",                     40, },    
                    { NULL, },
                },
            },
            {
                "Demonic", "RLDemonicSetBonusActive", false,
                {
                    { "RLDemonicCarapaceArmor",             40, },
                    { "RLDemonicBoots",                     40, },
                    { NULL, },
                },
            },
            {
                "Tactical", "RLTacticalSetBonusActive", false,
                {
                    { "RLTacticalArmor",                    40, },
                    { "RLTacticalBoots",                    40, },
                    { NULL, },
                },
            },
            {
                "Gothic", "RLGothicSetBonusActive", false,
                {
                    { "RLGothicArmor",                      40, },
                    { "RLGothicBoots",                      40, },
                    { NULL, },
                },
            },
            {
                "Phaseshift", "RLPhaseshiftSetBonusActive", false,
                {
                    { "RLPhaseshiftArmor",                  40, },
                    { "RLPhaseshiftBoots",                  40, },
                    { NULL, },
                },
            },
            {
                "Roysten", "RLRoystenSetBonusActive", false,
                {
                    { "RLRoystensCommandArmor",             25, },
                    { "RLRoystensCombatBoots",              25, },
                    { NULL, },
                },
            },
            {
                "Architect", "RLArchitectSetBonusActive", false,
                {
                    { "RLSoloOperativeSuitArmor",           25, },
                    { "RLSoloOperativeBoots",               25, },
                    { NULL, },
                },
            },
            {
                "Torgue", "RLTorgueSetBonusActive", false,
                {
                    { "RLTorgueBlastplateArmor",            25, },
                    { "RLTorgueBlastBoots",                 25, },
                    { NULL, },
                },
            },
            {
                "Enclave", "RLEnclaveSetBonusActive", false,
                {
                    { "RLNuclearPowerArmor",                25, },
                    { "RLNuclearPowerBoots",                25, },
                    { NULL, },
                },
            },
            {
                "Sentry Sentinel", "RLSentrySentinelSetBonusActive", false,
                {
                    { "RLTacticalAssemblerSuitArmor",       25, },
                    { "RLTacticalAssemblerBoots",           25, },
                    { NULL, },
                },
            },
            {
                "Sensible Strategist", "RLSensibleStrategistSetBonusActive", false,
                {
                    { "RLTacticalAssemblerSuitArmor",       25, },
                    { "RLTacticalAssemblerBoots",           25, },
                    { NULL, },
                },
            },
            {
                "Lava", "RLLavaSetBonusActive", false,
                {
                    { "RLLavaArmor",                        20, },
                    { "RLLavaBoots",                        20, },
                    { NULL, },
                },
            },
            {
                "Inquisitor", "RLInquisitorsSetBonusActive", false,
                {
                    { "RLMaleksArmor",                      20, },
                    { "RLNyarlaptotepsBoots",               20, },
                    { NULL, },
                },
            },
            {
                "Cerberus", "RLCerberusSetBonusActive", false,
                {
                    { "RLTristarBlaster",                   25, },
                    { "RLCerberusArmor",                    25, },
                    { "RLCerberusBoots",                    25, },
                    { NULL, },
                },
            },
            {
                "Angelic", "RLAngelicAttireSetBonusActive", false,
                {
                    { "RLAngelicArmor",                     10, },
                    { "RLAngelicBoots",                     10, },
                    { NULL, },
                },
            },
            {
                "Nuclear", "RLNuclearWeaponSetBonusActive", false,
                {
                    { "RLNuclearPlasmaPistol",              25, },
                    { "RLNuclearPlasmaRifle",               15, },
                    { "RLNuclearBFG9000",                   10, },
                    { "RLNuclearOnslaught",                 0.2, },
                    { "RLNuclearArmor",                     20, },
                    { "RLNuclearPowerArmor",                3, },
                    { "RLHighPowerNuclearPlasmaPistol",     0, },
                    { "RLStormNuclearPlasmaPistol",         0, },
                    { "RLSuperchargedNuclearPlasmaPistol",  0, },
                    { "RLNuclearPlasmaShotgun",             0, },
                    { "RLNuclearPlasmaRifleMkII",           0, },
                    { "RLAssaultRifleNuclearPlasmaRifle",   0, },
                    { "RLBurstCannonNuclearPlasmaRifle",    0, },
                    { "RLHighPowerNuclearPlasmaRifle",      0, },
                    { "RLHighPowerNuclearBFG9000",          0, },
                    { "RLNuclearVBFG9000",                  0, },
                    { "RLNuclearBiggestFuckingGun",         0, },
                    { "RLNuclearPlasmaRevolver",            0, },
                    { "RLNuclearOnslaught",                 0, },
                    { NULL, },
                },
            },
            {
                "Alucard", "RLAntiFreakJackalDemonArtifacts", false,
                {
                    { "RLAntiFreakJackal",                  10, },
                    { "RLHellsingARMSCasull",               10, },
                    { NULL, },
                },
            },
            {
                "Security Officer", "RLMarathonShotgunsDemonArtifacts", true,
                {
                    { "RLMarathonShotguns",                 10, },
                    { NULL, },
                },
            },
        };
        
        int SetAmount = Amount;
        
        if (GetCVar("drpg_debug"))
            Log("\CdDEBUG: \C-Checking DRLA sets...");
        
        for (int i = 0; i < MAX_DRLA_SETS; i++)
        {
            if (GetCVar("drpg_debug"))
                Log("\CdDEBUG: \C-Set \Cd%d\C-: \Cd%S", i + 1, SetItems[i].Name);
            
            // Skip this set if you already have it
            if (CheckInventory(SetItems[i].Token)) continue;
            
            bool HaveItem = false;
            for (int j = 0; SetItems[i].Items[j].Name != NULL; j++)
            {
                if (GetCVar("drpg_debug"))
                    Log("\CdDEBUG: \C-Set \Cd%d \C-Item \Cd%d\C-: \Cd%S", i + 1, j + 1, SetItems[i].Items[j].Name);
                
                if (CheckInventory(SetItems[i].Items[j].Name) || CheckInventory(StrParam("%SToken", SetItems[i].Items[j].Name)))
                {
                    HaveItem = true;
                    if (GetCVar("drpg_debug"))
                        Log("\CdDEBUG: \C-Have this item, continue...");
                    break;
                }
            }
            
            if (HaveItem)
            {
                for (int j = 0; SetItems[i].Items[j].Name != NULL; j++)
                {
                    str PickupItemName = StrParam("%SPickup", SetItems[i].Items[j].Name);
                    ItemInfoPtr ItemPtr = FindItem(PickupItemName);
                    fixed Chance = SetItems[i].Items[j].Chance * (1.0 + (Crates[ID].Rarity * 0.125));
                    fixed Pick = RandomFixed(0.0, 100.0);
                    
                    // Skip this item if you have it and it's not a dual/synthfire set
                    if (!SetItems[i].Synthfire && CheckInventory(SetItems[i].Items[j].Name) || CheckInventory(StrParam("%SToken", SetItems[i].Items[j].Name))) continue;
                    
                    // Stop checking items if we've filled the crate
                    if (SetAmount >= CRATE_MAX_ITEMS)
                    {
                        if (GetCVar("drpg_debug"))
                            Log("\CdDEBUG: \C-Crate is full! Stopping set checks");
                        return;
                    }
                    
                    if (GetCVar("drpg_debug"))
                        Log("\CdDEBUG: \C-Set \Cd%d \C-Item \Cd%d\C-: \Cd%S \C-(Chance: \Cf%.2k%% \C-/ Pick: \Cf%.2k%%\C-)", i + 1, j + 1, SetItems[i].Items[j].Name, Chance, Pick);
                    
                    if (Pick <= Chance)
                    {
                        // Couldn't find the item
                        if (ItemPtr == GetBlankItem()) continue;
                        
                        Crates[ID].Active[SetAmount] = true;
                        Crates[ID].Item[SetAmount] = ItemPtr;
                        
                        if (GetCVar("drpg_debug"))
                            Log("\CdDEBUG: \CfSet Item Spawned!");
                        
                        SetAmount++;
                    }
                }
            }
        }
    }
}

void CrateTakeItem()
{
    bool Spawned = SpawnForced(Crates[Player.CrateID].Item[Player.CrateIndex]->Actor, GetActorX(0), GetActorY(0), GetActorZ(0), 0, 0);
    
    if (Spawned)
    {
        SetActorVelocity(Player.TID, 0.01, 0.01, 0, true, false);
        
        Crates[Player.CrateID].Active[Player.CrateIndex] = false;
        Crates[Player.CrateID].Item[Player.CrateIndex] = NULL;
    }
}

bool CrateEmpty(int ID)
{
    for (int i = 0; i < CRATE_MAX_ITEMS; i++)
        if (Crates[ID].Active[i])
            return false;
    
    return true;
}

// TODO: Tried to make this a more generic ItemSpace CrateInfo *FindCrate, but DH-ACC bitched, yell at David
// TODO: Actually fix this for GDCC
bool CrateEmptyTID(int TID)
{
    for (int i = 0; i < CrateID; i++)
        if (Crates[i].TID == TID)
            return Crates[i].Empty;
    
    return false;
}

void DrawCrate()
{
    // Width/Height
    int Width = 9;
    int Height = 6;
    
    // Coordinates
    fixed BaseX;
    fixed BaseY;
    fixed BoxX;
    fixed BoxY;
    fixed X;
    fixed Y;
    fixed IconX;
    fixed IconY;
    
    // Title
    SetFont("BIGFONT");
    HudMessage("UAC Supply Crate (%S\C-)", CrateRarityNames[Crates[Player.CrateID].Rarity]);
    EndHudMessage(HUDMSG_PLAIN, 0, "Green", 24.1, 24.0, 0.05);
    
    for (int i = 0; i < Height; i++)
    {
        // Reset base X
        BaseX = 0.0;
        
        for (int j = 0; j < Width; j++)
        {
            int Index = j + (i * Width);
            
            ItemInfoPtr Item = Crates[Player.CrateID].Item[Index];
            bool Active = Crates[Player.CrateID].Active[Index];
            str Icon = Item->Sprite.Name;
            str Name = Item->Name;
            int IconXOff = Item->Sprite.XOff;
            int IconYOff = Item->Sprite.YOff;
            
            // Setup X and Y
            X = 26.0 + BaseX;
            Y = 50.0 + BaseY;
            BoxX = 48.0 + BaseX;
            BoxY = 72.0 + BaseY;
            IconX = 48.0 + BaseX;
            IconY = 72.0 + BaseY;
            IconX += IconXOff;
            IconY += IconYOff;
            
            // Icon
            SetHudClipRect(X, Y, 44, 44);
            if (Active)
                if (Player.CrateIndex == Index)
                    PrintSpritePulse(Icon, 0, IconX, IconY, 0.75, 32.0, 0.25);
                else
                    PrintSprite(Icon, 0, IconX, IconY, 0.05);
            SetHudClipRect(0, 0, 0, 0);
            
            // Box
            if (Player.CrateIndex == Index)
            {
                if (Active)
                    PrintSprite("ItemBoxH", 0, BoxX, BoxY, 0.05);
                else
                    PrintSprite("ItemBoxR", 0, BoxX, BoxY, 0.05);
            }
            PrintSprite("ItemBoxB", 0, BoxX, BoxY, 0.05);
            
            // Item Name
            if (Player.CrateIndex == Index && Active)
            {
                SetFont("BIGFONT");
                HudMessage("%S", Name);
                EndHudMessage(HUDMSG_PLAIN, 0, "White", 24.1, 344.1, 0.05);
            }
            
            // Increment X
            BaseX += 48.0;
        }
        
        // Increment Y
        BaseY += 48.0;
    }
}

void CrateInput()
{
    int Buttons = GetPlayerInput(PlayerNumber(), INPUT_BUTTONS);
    int OldButtons = GetPlayerInput(PlayerNumber(), INPUT_OLDBUTTONS);
    
    if (Buttons & BT_SPEED && !(OldButtons & BT_SPEED))
    {
        ActivatorSound("crate/close", 127);
        Player.CrateOpen = false;
        
        // Set the crate to it's inactive state if it's empty
        if (CrateEmpty(Player.CrateID))
        {
            SetActorState(Crates[Player.CrateID].TID, "Empty");
            Crates[Player.CrateID].Empty = true;
        }
    }
    if (Buttons & BT_USE && !(OldButtons & BT_USE))
    {
        ActivatorSound("menu/move", 127);
        CrateTakeItem();
    }
    if (Buttons & BT_FORWARD && !(OldButtons & BT_FORWARD))
    {
        ActivatorSound("menu/move", 127);
        Player.CrateIndex -= 9;
        if (Player.CrateIndex < 0) Player.CrateIndex = 0;
    }
    if (Buttons & BT_BACK && !(OldButtons & BT_BACK))
    {
        ActivatorSound("menu/move", 127);
        Player.CrateIndex += 9;
        if (Player.CrateIndex > CRATE_MAX_ITEMS - 1) Player.CrateIndex = CRATE_MAX_ITEMS - 1;
    }
    if ((Buttons & BT_LEFT && !(OldButtons & BT_LEFT)) ||
        (Buttons & BT_MOVELEFT && !(OldButtons & BT_MOVELEFT)))
    {
        ActivatorSound("menu/move", 127);
        Player.CrateIndex--;
        if (Player.CrateIndex < 0) Player.CrateIndex = 0;
    }
    if ((Buttons & BT_RIGHT && !(OldButtons & BT_RIGHT)) ||
        (Buttons & BT_MOVERIGHT && !(OldButtons & BT_MOVERIGHT)))
    {
        ActivatorSound("menu/move", 127);
        Player.CrateIndex++;
        if (Player.CrateIndex > CRATE_MAX_ITEMS - 1) Player.CrateIndex = CRATE_MAX_ITEMS - 1;
    }
}

NamedScript Console void DumpCrate()
{
    for (int i = 0; i < CrateID; i++)
    {
        Log("\Cd  ===== Crate %d =====", i);
        Log("Amount: %d", Crates[i].Amount);
        Log("Rarity: %d", Crates[i].Rarity);
        
        for (int j = 0; j < CRATE_MAX_ITEMS; j++)
        {
            if (!Crates[i].Active[j]) continue;
            
            Log("%d. %S (%S)", j, Crates[i].Item[j]->Name, Crates[i].Item[j]->Actor);
        }
    }
}
