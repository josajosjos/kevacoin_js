// Copyright (c) 2012-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>

#include <consensus/consensus.h>
#include <random.h>

bool CCoinsView::GetCoin(const COutPoint &outpoint, Coin &coin) const { return false; }
uint256 CCoinsView::GetBestBlock() const { return uint256(); }
std::vector<uint256> CCoinsView::GetHeadBlocks() const { return std::vector<uint256>(); }
bool CCoinsView::GetNamespace(const valtype &nameSpace, CKevaData &data) const { return false; }
bool CCoinsView::GetName(const valtype &nameSpace, const valtype &key, CKevaData &data) const { return false; }
bool CCoinsView::GetNamesForHeight(unsigned nHeight, std::set<valtype>& names) const { return false; }
CKevaIterator* CCoinsView::IterateKeys(const valtype& nameSpace) const { assert (false); }
CKevaIterator* CCoinsView::IterateAssociatedNamespaces(const valtype& nameSpace) const { assert (false); }
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, const CKevaCache &names) { return false; }
CCoinsViewCursor *CCoinsView::Cursor() const { return nullptr; }
bool CCoinsView::ValidateKevaDB() const {
    // TODO: return false, and implement it in txdb.cpp.
    // Need to figure out what to check.
    return true;
}

bool CCoinsView::HaveCoin(const COutPoint &outpoint) const
{
    Coin coin;
    return GetCoin(outpoint, coin);
}

CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }
bool CCoinsViewBacked::GetCoin(const COutPoint &outpoint, Coin &coin) const { return base->GetCoin(outpoint, coin); }
bool CCoinsViewBacked::HaveCoin(const COutPoint &outpoint) const { return base->HaveCoin(outpoint); }
uint256 CCoinsViewBacked::GetBestBlock() const { return base->GetBestBlock(); }
std::vector<uint256> CCoinsViewBacked::GetHeadBlocks() const { return base->GetHeadBlocks(); }
bool CCoinsViewBacked::GetNamespace(const valtype &nameSpace, CKevaData &data) const {
    return base->GetNamespace(nameSpace, data);
}
bool CCoinsViewBacked::GetName(const valtype &nameSpace, const valtype &key, CKevaData &data) const {
    return base->GetName(nameSpace, key, data);
}
bool CCoinsViewBacked::GetNamesForHeight(unsigned nHeight, std::set<valtype>& names) const {
    return base->GetNamesForHeight(nHeight, names);
}
CKevaIterator* CCoinsViewBacked::IterateKeys(const valtype& nameSpace) const { return base->IterateKeys(nameSpace); }
CKevaIterator* CCoinsViewBacked::IterateAssociatedNamespaces(const valtype& nameSpace) const { return base->IterateAssociatedNamespaces(nameSpace); }
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock, const CKevaCache &names) {
    return base->BatchWrite(mapCoins, hashBlock, names);
}
CCoinsViewCursor *CCoinsViewBacked::Cursor() const { return base->Cursor(); }
size_t CCoinsViewBacked::EstimateSize() const { return base->EstimateSize(); }
bool CCoinsViewBacked::ValidateKevaDB() const { return base->ValidateKevaDB(); }

SaltedOutpointHasher::SaltedOutpointHasher() : k0(GetRand(std::numeric_limits<uint64_t>::max())), k1(GetRand(std::numeric_limits<uint64_t>::max())) {}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), cachedCoinsUsage(0) {}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}

CCoinsMap::iterator CCoinsViewCache::FetchCoin(const COutPoint &outpoint) const {
    CCoinsMap::iterator it = cacheCoins.find(outpoint);
    if (it != cacheCoins.end())
        return it;
    Coin tmp;
    if (!base->GetCoin(outpoint, tmp))
        return cacheCoins.end();
    CCoinsMap::iterator ret = cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::forward_as_tuple(std::move(tmp))).first;
    if (ret->second.coin.IsSpent()) {
        // The parent only has an empty entry for this outpoint; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coin.DynamicMemoryUsage();
    return ret;
}

bool CCoinsViewCache::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it != cacheCoins.end()) {
        coin = it->second.coin;
        return !coin.IsSpent();
    }
    return false;
}

void CCoinsViewCache::AddCoin(const COutPoint &outpoint, Coin&& coin, bool possible_overwrite) {
    assert(!coin.IsSpent());
    if (coin.out.scriptPubKey.IsUnspendable()) return;
    CCoinsMap::iterator it;
    bool inserted;
    std::tie(it, inserted) = cacheCoins.emplace(std::piecewise_construct, std::forward_as_tuple(outpoint), std::tuple<>());
    bool fresh = false;
    if (!inserted) {
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    }
    if (!possible_overwrite) {
        if (!it->second.coin.IsSpent()) {
            throw std::logic_error("Adding new coin that replaces non-pruned entry");
        }
        fresh = !(it->second.flags & CCoinsCacheEntry::DIRTY);
    }
    it->second.coin = std::move(coin);
    it->second.flags |= CCoinsCacheEntry::DIRTY | (fresh ? CCoinsCacheEntry::FRESH : 0);
    cachedCoinsUsage += it->second.coin.DynamicMemoryUsage();
}

void AddCoins(CCoinsViewCache& cache, const CTransaction &tx, int nHeight, bool check) {
    bool fCoinbase = tx.IsCoinBase();
    const uint256& txid = tx.GetHash();
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        bool overwrite = check ? cache.HaveCoin(COutPoint(txid, i)) : fCoinbase;
        // Always set the possible_overwrite flag to AddCoin for coinbase txn, in order to correctly
        // deal with the pre-BIP30 occurrences of duplicate coinbase transactions.
        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase), overwrite);
    }
}

bool CCoinsViewCache::SpendCoin(const COutPoint &outpoint, Coin* moveout) {
    CCoinsMap::iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end()) return false;
    cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    if (moveout) {
        *moveout = std::move(it->second.coin);
    }
    if (it->second.flags & CCoinsCacheEntry::FRESH) {
        cacheCoins.erase(it);
    } else {
        it->second.flags |= CCoinsCacheEntry::DIRTY;
        it->second.coin.Clear();
    }
    return true;
}

static const Coin coinEmpty;

const Coin& CCoinsViewCache::AccessCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end()) {
        return coinEmpty;
    } else {
        return it->second.coin;
    }
}

bool CCoinsViewCache::HaveCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

bool CCoinsViewCache::HaveCoinInCache(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = cacheCoins.find(outpoint);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

uint256 CCoinsViewCache::GetBestBlock() const {
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    hashBlock = hashBlockIn;
}

bool CCoinsViewCache::GetNamespace(const valtype &nameSpace, CKevaData &data) const {
    if (cacheNames.GetNamespace(nameSpace, data)) {
        return true;
    }
    return base->GetNamespace(nameSpace, data);
}

bool CCoinsViewCache::GetName(const valtype &nameSpace, const valtype &key, CKevaData &data) const {
    if (cacheNames.isDeleted(nameSpace, key))
        return false;
    if (cacheNames.get(nameSpace, key, data))
        return true;

    /* Note: This does not attempt to cache name queries.  The cache
       only keeps track of changes!  */

    return base->GetName(nameSpace, key, data);
}

bool CCoinsViewCache::GetNamesForHeight(unsigned nHeight, std::set<valtype>& names) const {
    /* Query the base view first, and then apply the cached changes (if
       there are any).  */

    if (!base->GetNamesForHeight(nHeight, names))
        return false;

    cacheNames.updateNamesForHeight(nHeight, names);
    return true;
}

CKevaIterator* CCoinsViewCache::IterateKeys(const valtype& nameSpace) const {
    return cacheNames.iterateKeys(base->IterateKeys(nameSpace));
}

CKevaIterator* CCoinsViewCache::IterateAssociatedNamespaces(const valtype& nameSpace) const {
    return cacheNames.IterateAssociatedNamespaces(base->IterateAssociatedNamespaces(nameSpace));
}

/* undo is set if the change is due to disconnecting blocks / going back in
   time.  The ordinary case (!undo) means that we update the name normally,
   going forward in time.  This is important for keeping track of the
   name history.  */
void CCoinsViewCache::SetKeyValue(const valtype &nameSpace, const valtype &key, const CKevaData& data, bool undo)
{
    cacheNames.set(nameSpace, key, data);

    // Handle namespace association.
    valtype associdateNamespace;
    if (!cacheNames.getAssociateNamespaces(key, associdateNamespace)) {
        return;
    }

    CKevaData dummyData;
    if (!GetNamespace(associdateNamespace, dummyData)) {
        return;
    }
    cacheNames.associateNamespaces(nameSpace, associdateNamespace, data);
}

void CCoinsViewCache::DeleteKey(const valtype &nameSpace, const valtype &key) {
    CKevaData oldData;
    if (!GetName(nameSpace, key, oldData)) {
        assert(false);
    }
    cacheNames.remove(nameSpace, key);

    // Handle namespace association.
    valtype associdateNamespace;
    if (!cacheNames.getAssociateNamespaces(key, associdateNamespace)) {
        return;
    }

    CKevaData dummyData;
    if (!GetNamespace(associdateNamespace, dummyData)) {
        return;
    }
    cacheNames.disassociateNamespaces(nameSpace, associdateNamespace);
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlockIn, const CKevaCache &names) {
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end(); it = mapCoins.erase(it)) {
        // Ignore non-dirty entries (optimization).
        if (!(it->second.flags & CCoinsCacheEntry::DIRTY)) {
            continue;
        }
        CCoinsMap::iterator itUs = cacheCoins.find(it->first);
        if (itUs == cacheCoins.end()) {
            // The parent cache does not have an entry, while the child does
            // We can ignore it if it's both FRESH and pruned in the child
            if (!(it->second.flags & CCoinsCacheEntry::FRESH && it->second.coin.IsSpent())) {
                // Otherwise we will need to create it in the parent
                // and move the data up and mark it as dirty
                CCoinsCacheEntry& entry = cacheCoins[it->first];
                entry.coin = std::move(it->second.coin);
                cachedCoinsUsage += entry.coin.DynamicMemoryUsage();
                entry.flags = CCoinsCacheEntry::DIRTY;
                // We can mark it FRESH in the parent if it was FRESH in the child
                // Otherwise it might have just been flushed from the parent's cache
                // and already exist in the grandparent
                if (it->second.flags & CCoinsCacheEntry::FRESH) {
                    entry.flags |= CCoinsCacheEntry::FRESH;
                }
            }
        } else {
            // Assert that the child cache entry was not marked FRESH if the
            // parent cache entry has unspent outputs. If this ever happens,
            // it means the FRESH flag was misapplied and there is a logic
            // error in the calling code.
            if ((it->second.flags & CCoinsCacheEntry::FRESH) && !itUs->second.coin.IsSpent()) {
                throw std::logic_error("FRESH flag misapplied to cache entry for base transaction with spendable outputs");
            }

            // Found the entry in the parent cache
            if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coin.IsSpent()) {
                // The grandparent does not have an entry, and the child is
                // modified and being pruned. This means we can just delete
                // it from the parent.
                cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                cacheCoins.erase(itUs);
            } else {
                // A normal modification.
                cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                itUs->second.coin = std::move(it->second.coin);
                cachedCoinsUsage += itUs->second.coin.DynamicMemoryUsage();
                itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                // NOTE: It is possible the child has a FRESH flag here in
                // the event the entry we found in the parent is pruned. But
                // we must not copy that FRESH flag to the parent as that
                // pruned state likely still needs to be communicated to the
                // grandparent.
            }
        }
    }
    hashBlock = hashBlockIn;
    cacheNames.apply(names);
    return true;
}

bool CCoinsViewCache::Flush() {
    /* This function is called when validating the name mempool, and BatchWrite
       actually fails if hashBlock is not set.  Thus we have to make sure here
       that it is a valid no-op when nothing is cached.  */
    if (hashBlock.IsNull() && cacheCoins.empty() && cacheNames.empty())
        return true;

    bool fOk = base->BatchWrite(cacheCoins, hashBlock, cacheNames);
    cacheCoins.clear();
    cachedCoinsUsage = 0;
    cacheNames.clear();
    return fOk;
}

void CCoinsViewCache::Uncache(const COutPoint& hash)
{
    CCoinsMap::iterator it = cacheCoins.find(hash);
    if (it != cacheCoins.end() && it->second.flags == 0) {
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
        cacheCoins.erase(it);
    }
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    return cacheCoins.size();
}

CAmount CCoinsViewCache::GetValueIn(const CTransaction& tx) const
{
    if (tx.IsCoinBase())
        return 0;

    CAmount nResult = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        nResult += AccessCoin(tx.vin[i].prevout).out.nValue;

    return nResult;
}

bool CCoinsViewCache::HaveInputs(const CTransaction& tx) const
{
    if (!tx.IsCoinBase()) {
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            if (!HaveCoin(tx.vin[i].prevout)) {
                return false;
            }
        }
    }
    return true;
}

static const size_t MIN_TRANSACTION_OUTPUT_WEIGHT = WITNESS_SCALE_FACTOR * ::GetSerializeSize(CTxOut(), SER_NETWORK, PROTOCOL_VERSION);
static const size_t MAX_OUTPUTS_PER_BLOCK = MAX_BLOCK_WEIGHT / MIN_TRANSACTION_OUTPUT_WEIGHT;

const Coin& AccessByTxid(const CCoinsViewCache& view, const uint256& txid)
{
    COutPoint iter(txid, 0);
    while (iter.n < MAX_OUTPUTS_PER_BLOCK) {
        const Coin& alternate = view.AccessCoin(iter);
        if (!alternate.IsSpent()) return alternate;
        ++iter.n;
    }
    return coinEmpty;
}
