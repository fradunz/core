#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Policies/SingletonImp.h"
#include "Item.h"
#include "AuctionHouseMgr.h"
#include "ObjectMgr.h"
#include "AuctionHouseBotMgr.h"
#include "Config/Config.h"
#include "Chat.h"
#include "Utilities/Random.h"

INSTANTIATE_SINGLETON_1(AuctionHouseBotMgr);

AuctionHouseBotMgr::~AuctionHouseBotMgr()
{
    m_items.clear();

    if (m_config)
        m_config.reset();
}

void AuctionHouseBotMgr::Load()
{
    /* 1 - DELETE */
    m_items.clear();
    m_loaded = false;

    if (m_config)
        m_config.reset();

    /*2 - LOAD */
    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `item`, `stack`, `bid`, `buyout` FROM `auctionhousebot`"));

    if (!result)
    {
        BarGoLink bar(1);
        bar.step();

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 AuctionHouseBot items");
        return;
    }

    uint32 count = 0;
    BarGoLink bar(result->GetRowCount());

    Field* fields;
    do
    {
        bar.step();
        AuctionHouseBotEntry e;
        fields    = result->Fetch();
        e.item    = fields[0].GetUInt32();
        e.stack   = fields[1].GetUInt32();
        e.bid     = fields[2].GetUInt32();
        e.buyout  = fields[3].GetUInt32();

        m_items.push_back(e);

        ++count;
    }
    while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u AuctionHouseBot items", count);

    /* CONFIG */
    m_config                 = std::make_unique<AuctionHouseBotConfig>();
    m_config->enable         = sConfig.GetBoolDefault("AHBot.Enable", false);
    m_config->ahfid          = sConfig.GetIntDefault("AHBot.ah.fid", 120);
    m_config->itemcount      = sConfig.GetIntDefault("AHBot.itemcount", 2);

    m_auctionHouseEntry = sAuctionMgr.GetAuctionHouseEntry(m_config->ahfid);
    if (!m_auctionHouseEntry)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::Load() : No auction house for faction %u.", m_config->ahfid);
        return;
    }
    m_loaded = true;
}

void AuctionHouseBotMgr::Update(bool force /* = false */)
{
    if (!m_loaded)
        return;

    ASSERT(m_config);
    ASSERT(m_auctionHouseEntry);

    if (!(m_config->enable || force))
        return;

    if (m_items.empty())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "AHBot::Update() : Bad config or empty table.");
        return;
    }

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(m_auctionHouseEntry);
    if (!auctionHouse)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "AHBot::Update() : No auction house for faction %u.", m_config->ahfid);
        return;
    }

    uint32 auctions     = auctionHouse->GetCount();
    uint32 items        = m_config->itemcount;
    uint32 entriesCount = m_items.size();

    while (auctions < items)
    {
        AuctionHouseBotEntry item = m_items[urand(0, entriesCount - 1)];
        // Makes single items and the most expensive appear at half the rate
        // of materials and consumables
        if ((item.stack == 1 || item.buyout >= 1000000 ) && urand(0, 1))
            continue;
        AddItem(item, auctionHouse);
        auctions++;
    }
}

void AuctionHouseBotMgr::AddItem(AuctionHouseBotEntry e, AuctionHouseObject *auctionHouse)
{
    ASSERT(m_auctionHouseEntry);

    uint32 itemStack = 1;
    float priceFactor = 1.0f;
    uint32 itemBid = 100;
    uint32 itemBuyOut = 1000;

    ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(e.item);
    if (prototype == nullptr)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::AddItem() : Item %u does not exist.", e.item);
        return;
    }

    Item* item = Item::CreateItem(e.item, 1);
    if (!item)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::AddItem() : Cannot create item.");
        return;
    }

    //sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::AddItem() : Adding item %u.", e.item);

    uint32 randomPropertyId = Item::GenerateItemRandomPropertyId(e.item);
    if (randomPropertyId != 0)
        item->SetItemRandomProperties(randomPropertyId);

    uint32 etime = urand(1, 5);
    switch (etime)
    {
        case 1:
            etime = urand(7200, 28800);
            break;
        case 2:
        case 3:
            etime = urand(28800, 57600);
            break;
        case 4:
        case 5:
            etime = 86400;
            break;
        default:
            etime = 86400;
            break;
    }

    if (e.stack > 1)
    {
        switch (urand(1, 5))
        {
            case 1:
                itemStack = urand(1, e.stack);
                break;
            case 2:
                itemStack = 1;
                break;
            case 3:
                itemStack = e.stack / 2;
                break;
            case 4:
            case 5:
                itemStack = e.stack;
                break;
            default:
                itemStack = 1;
                break;
        }
        priceFactor = (float)itemStack / (float)e.stack;
    }
    else
        itemStack = e.stack;
    
    if (e.bid == 0)
        itemBid = prototype->SellPrice * 5 * itemStack;
    else
        itemBid = e.bid;
    itemBid = itemBid * priceFactor * (urand(10,30) * 0.05f);
    
    if (e.buyout == 0)
        itemBuyOut = itemBid * 2;
    else
        itemBuyOut = e.buyout;
    itemBuyOut = itemBuyOut * priceFactor * (urand(16,30) * 0.05f);
    if (!urand(0, 4))
        itemBuyOut--;
        
    if (itemBid > itemBuyOut)
        itemBid = itemBuyOut - 1;

    item->SetCount(itemStack);

    uint32 dep = sAuctionMgr.GetAuctionDeposit(m_auctionHouseEntry, etime, item);

    AuctionEntry* auctionEntry       = new AuctionEntry;
    auctionEntry->Id                 = sObjectMgr.GenerateAuctionID();
    auctionEntry->auctionHouseEntry  = m_auctionHouseEntry;
    auctionEntry->itemGuidLow        = item->GetGUIDLow();
    auctionEntry->itemTemplate       = item->GetEntry();
    auctionEntry->owner              = 0;
    auctionEntry->startbid           = itemBid;
    auctionEntry->buyout             = itemBuyOut;
    auctionEntry->bidder             = 0;
    auctionEntry->bid                = 0;
    auctionEntry->deposit            = dep;
    auctionEntry->depositTime        = time(nullptr);
    auctionEntry->expireTime         = (time_t) etime + time(nullptr);

    item->SaveToDB();

    sAuctionMgr.AddAItem(item);
    auctionHouse->AddAuction(auctionEntry);
    auctionEntry->SaveToDB();
}

AuctionHouseBotCreateResult AuctionHouseBotMgr::Create(uint32 itemId, bool allowDuplicate /* = false */)
{
    ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(itemId);
    if (prototype == nullptr)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::Create() : Item %u does not exist.", itemId);
        return AHBOT_CREATE_ITEM_NOT_FOUND;
    }

    // Check the table itself, m_items is only refreshed on reload
    if (!allowDuplicate)
    {
        std::unique_ptr<QueryResult> existing(WorldDatabase.PQuery("SELECT 1 FROM `auctionhousebot` WHERE `item` = %u LIMIT 1", itemId));
        if (existing)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::Create() : Item %u already has an auction template.", itemId);
            return AHBOT_CREATE_DUPLICATE;
        }
    }

    uint32 stack = prototype->GetMaxStackSize();
    uint32 multi = (prototype->Quality * 3) + 3;
    uint32 bid = prototype->SellPrice * multi * stack;
    uint32 buyout = bid * 2;

    if (!WorldDatabase.PExecute("INSERT INTO `auctionhousebot` (`item`, `stack`, `bid`, `buyout`) VALUES (%u, %u, %u, %u)", itemId, stack, bid, buyout))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "AHBot::Create() : Error creating auction for item %u.", itemId);
        return AHBOT_CREATE_DB_ERROR;
    }

    return AHBOT_CREATE_OK;
}

bool ChatHandler::HandleAHBotUpdateCommand(char *args)
{
    sAuctionHouseBotMgr.Update(true);
    SendSysMessage("[AHBot] Update finished.");
    return true;
}

bool ChatHandler::HandleAHBotReloadCommand(char *args)
{
    sAuctionHouseBotMgr.Load();
    SendSysMessage("[AHBot] Reload finished.");
    return true;
}

bool ChatHandler::HandleAHBotCreateCommand(char *args)
{
    ///- Get the command line arguments: <itemId> [allowDuplicate (0|1)]
    uint32 itemId;
    if (!ExtractUInt32(&args, itemId))
        return false;

    uint32 allowDuplicate;
    if (!ExtractOptUInt32(&args, allowDuplicate, 0) || allowDuplicate > 1)
        return false;

    switch (sAuctionHouseBotMgr.Create(itemId, allowDuplicate == 1))
    {
        case AHBOT_CREATE_OK:
            PSendSysMessage("[AHBot] Auction Created for item %u.", itemId);
            return true;
        case AHBOT_CREATE_ITEM_NOT_FOUND:
            PSendSysMessage("[AHBot] Item %u does not exist.", itemId);
            break;
        case AHBOT_CREATE_DUPLICATE:
            PSendSysMessage("[AHBot] Item %u already has an auction template. Use '.ahbot create %u 1' to create a duplicate.", itemId, itemId);
            break;
        case AHBOT_CREATE_DB_ERROR:
            PSendSysMessage("[AHBot] Error creating auction for item %u.", itemId);
            break;
    }

    SetSentErrorMessage(true);
    return false;
}
