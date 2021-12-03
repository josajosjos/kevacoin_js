// Copyright (c) 2014-2017 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Copyright (c) 2018 the Kevacoin Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <coins.h>
#include <chain.h>
#include <consensus/validation.h>
#include <keva/main.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/keva.h>
#include <txdb.h>
#include <txmempool.h>
#include <undo.h>
#include <validation.h>

#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <list>
#include <memory>

#include <stdint.h>

BOOST_FIXTURE_TEST_SUITE (keva_tests, TestingSetup)

/**
 * Utility function that returns a sample address script to use in the tests.
 * @return A script that represents a simple address.
 */
static CScript getTestAddress()
{
  const CTxDestination dest = DecodeDestination ("VMpwrs7oqLbASU3tbKehhGyvDZ4u3mwR4y");
  BOOST_CHECK (IsValidDestination (dest));

  return GetScriptForDestination (dest);
}

/* ************************************************************************** */

BOOST_AUTO_TEST_CASE(keva_scripts)
{
  const CScript addr = getTestAddress();
  const CKevaScript opNone(addr);
  BOOST_CHECK (!opNone.isKevaOp());
  BOOST_CHECK (opNone.getAddress() == addr);

  const valtype nameSpace = ValtypeFromString ("namespace-string");
  const valtype displayName = ValtypeFromString ("display name");
  CScript script;
  script = CKevaScript::buildKevaNamespace(addr, nameSpace, displayName);


  const CKevaScript opNew(script);
  BOOST_CHECK(opNew.isKevaOp());
  BOOST_CHECK(opNew.getAddress() == addr);
  BOOST_CHECK(!opNew.isAnyUpdate());
  BOOST_CHECK(opNew.getKevaOp() == OP_KEVA_NAMESPACE);
  BOOST_CHECK(opNew.getOpNamespace() == nameSpace);

  const uint256 txId = uint256S("0x78f49add562dc33e4cd61aa45c54012509ed4a53308908dd07f5634437939273");
  valtype actualNamespace;
  int n = 99999;
  script = CKevaScript::replaceKevaNamespace(script, txId, n, actualNamespace, Params(), true);
  const CKevaScript opReplace(script);
  BOOST_CHECK(opReplace.isKevaOp());
  BOOST_CHECK(opReplace.getAddress() == addr);
  BOOST_CHECK(!opReplace.isAnyUpdate());
  BOOST_CHECK(opReplace.getKevaOp() == OP_KEVA_NAMESPACE);

  auto vin = ToByteVector(txId);
  auto nVal = ValtypeFromString(std::to_string(n));
  vin.insert(vin.end(), nVal.begin(), nVal.end());

  valtype expectedNamespace = ToByteVector(Hash160(ToByteVector(vin)));
  const std::vector<unsigned char>& ns_prefix = Params().Base58Prefix(CChainParams::KEVA_NAMESPACE);
  expectedNamespace.insert(expectedNamespace.begin(), ns_prefix.begin(), ns_prefix.end());
  BOOST_CHECK(opReplace.getOpNamespace() == expectedNamespace);
  BOOST_CHECK(actualNamespace == expectedNamespace);

  const valtype key = ValtypeFromString ("key");
  const valtype value = ValtypeFromString ("value");
  script = CKevaScript::buildKevaPut(addr, nameSpace, key, value);

  const CKevaScript opKevaPut(script);
  BOOST_CHECK(opKevaPut.isKevaOp());
  BOOST_CHECK(opKevaPut.getAddress() == addr);
  BOOST_CHECK(opKevaPut.isAnyUpdate());
  BOOST_CHECK(opKevaPut.getKevaOp() == OP_KEVA_PUT);
  BOOST_CHECK(opKevaPut.getOpKey() == key);
  BOOST_CHECK(opKevaPut.getOpValue() == value);
}

#if 0
/* ************************************************************************** */

BOOST_AUTO_TEST_CASE (name_database)
{
  const valtype name1 = ValtypeFromString ("database-test-name-1");
  const valtype name2 = ValtypeFromString ("database-test-name-2");
  const valtype value = ValtypeFromString ("my-value");
  const CScript addr = getTestAddress();

  /* Choose two height values.  To verify that serialisation of the
     expiration entries works as it should, make sure that the values
     are ordered wrongly when serialised in little-endian.  */
  const unsigned height1 = 0x00ff;
  const unsigned height2 = 0x0142;

  CNameData dataHeight1, dataHeight2, data2;
  CScript updateScript = CNameScript::buildNameUpdate (addr, name1, value);
  const CNameScript nameOp(updateScript);
  dataHeight1.fromScript (height1, COutPoint (uint256(), 0), nameOp);
  dataHeight2.fromScript (height2, COutPoint (uint256(), 0), nameOp);

  std::set<valtype> setExpected, setRet;

  CCoinsViewCache& view = *pcoinsTip;

  setExpected.clear();
  setRet.clear();
  setRet.insert (name1);
  BOOST_CHECK (view.GetNamesForHeight (height2, setRet));
  BOOST_CHECK (setRet == setExpected);

  BOOST_CHECK (!view.GetName (name1, data2));
  view.SetKeyValue (name1, dataHeight2, false);
  BOOST_CHECK (view.GetName (name1, data2));
  BOOST_CHECK (dataHeight2 == data2);

  BOOST_CHECK (view.GetNamesForHeight (height1, setRet));
  BOOST_CHECK (setRet == setExpected);
  setExpected.insert (name1);
  BOOST_CHECK (view.GetNamesForHeight (height2, setRet));
  BOOST_CHECK (setRet == setExpected);

  BOOST_CHECK (view.Flush());
  BOOST_CHECK (view.GetName (name1, data2));
  BOOST_CHECK (dataHeight2 == data2);

  view.SetKeyValue (name2, dataHeight2, false);
  BOOST_CHECK (view.GetNamesForHeight (height2, setRet));
  setExpected.insert (name2);
  BOOST_CHECK (setRet == setExpected);

  view.DeleteKey (name1);
  BOOST_CHECK (!view.GetName (name1, data2));
  BOOST_CHECK (view.Flush());
  BOOST_CHECK (!view.GetName (name1, data2));

  BOOST_CHECK (view.GetNamesForHeight (height2, setRet));
  setExpected.erase (name1);
  BOOST_CHECK (setRet == setExpected);

  view.SetKeyValue (name2, dataHeight1, false);
  BOOST_CHECK (view.Flush());
  view.SetKeyValue (name1, dataHeight1, false);

  BOOST_CHECK (view.GetNamesForHeight (height2, setRet));
  setExpected.clear();
  BOOST_CHECK (setRet == setExpected);
  BOOST_CHECK (view.GetNamesForHeight (height1, setRet));
  setExpected.insert (name1);
  setExpected.insert (name2);
  BOOST_CHECK (setRet == setExpected);
}

/* ************************************************************************** */

/**
 * Define a class that can be used as "dummy" base name database.  It allows
 * iteration over its content, but always returns an empty range for that.
 * This is necessary to define a "purely cached" view, since the iteration
 * over CCoinsViewCache always calls through to the base iteration.
 */
class CDummyIterationView : public CCoinsView
{

private:

  /**
   * "Fake" name iterator returned.
   */
  class Iterator : public CKevaIterator
  {
  public:

    void
    seek (const valtype& start)
    {}

    bool
    next (valtype& name, CNameData& data)
    {
      return false;
    }

  };

public:

  CKevaIterator*
  IterateKeys() const
  {
    return new Iterator();
  }

};

/**
 * Helper class for testing name iteration.  It allows performing changes
 * to the name list, and mirrors them to a purely cached view, a name DB
 * and a name DB with cache that is flushed from time to time.  It compares
 * all of them to each other.
 */
class NameIterationTester
{

private:

  /** Type used for ordered lists of entries.  */
  typedef std::list<std::pair<valtype, CNameData> > EntryList;

  /** Name database view.  */
  CCoinsViewDB& db;
  /** Cached view based off the database.  */
  CCoinsViewCache hybrid;

  /** Dummy base view without real content.  */
  CDummyIterationView dummy;
  /**
   * Cache view based off the dummy.  This allows to test iteration
   * based solely on the cache.
   */
  CCoinsViewCache cache;

  /** Keep track of what the name set should look like as comparison.  */
  CNameCache::EntryMap data;

  /**
   * Keep an internal counter to build unique and changing CNameData
   * objects for testing purposes.  The counter value will be used
   * as the name's height in the data.
   */
  unsigned counter;

  /**
   * Verify consistency of the given view with the expected data.
   * @param view The view to check against data.
   */
  void verify (const CCoinsView& view) const;

  /**
   * Get a new CNameData object for testing purposes.  This also
   * increments the counter, so that each returned value is unique.
   * @return A new CNameData object.
   */
  CNameData getNextData();

  /**
   * Iterate all names in a view and return the result as an ordered
   * list in the way they appeared.
   * @param view The view to iterate over.
   * @param start The start name.
   * @return The resulting entry list.
   */
  static EntryList getNamesFromView (const CCoinsView& view,
                                     const valtype& start);

  /**
   * Return all names that are produced by the given iterator.
   * @param iter The iterator to use.
   * @return The resulting entry list.
   */
  static EntryList getNamesFromIterator (CKevaIterator& iter);

public:

  /**
   * Construct the tester with the given database view to use.
   * @param base The database coins view to use.
   */
  explicit NameIterationTester (CCoinsViewDB& base);

  /**
   * Verify consistency of all views.  This also flushes the hybrid cache
   * between verifying it and the base db view.
   */
  void verify();

  /**
   * Add a new name with created dummy data.
   * @param n The name to add.
   */
  void add (const std::string& n);

  /**
   * Update the name with new dummy data.
   * @param n The name to update.
   */
  void update (const std::string& n);

  /**
   * Delete the name.
   * @param n The name to delete.
   */
  void remove (const std::string& n);

};

NameIterationTester::NameIterationTester (CCoinsViewDB& base)
  : db(base), hybrid(&db), dummy(), cache(&dummy), data(), counter(100)
{
  // Nothing else to do.
}

CNameData
NameIterationTester::getNextData()
{
  const CScript addr = getTestAddress();
  const valtype name = ValtypeFromString ("dummy");
  const valtype value = ValtypeFromString ("abc");
  const CScript updateScript = CNameScript::buildNameUpdate (addr, name, value);
  const CNameScript nameOp(updateScript);

  CNameData res;
  res.fromScript (++counter, COutPoint (uint256(), 0), nameOp);

  return res;
}

void
NameIterationTester::verify (const CCoinsView& view) const
{
  /* Try out everything with all names as "start".  This thoroughly checks
     that also the start implementation is correct.  It also checks using
     a single iterator and seeking vs using a fresh iterator.  */

  valtype start;
  EntryList remaining(data.begin(), data.end());

  /* Seek the iterator to the end first for "maximum confusion".  This ensures
     that seeking to valtype() works.  */
  std::unique_ptr<CKevaIterator> iter(view.IterateKeys());
  const valtype end = ValtypeFromString ("zzzzzzzzzzzzzzzz");
  {
    valtype name;
    CNameData nameData;

    iter->seek (end);
    BOOST_CHECK (!iter->next (name, nameData));
  }

  while (true)
    {
      EntryList got = getNamesFromView (view, start);
      BOOST_CHECK (got == remaining);

      iter->seek (start);
      got = getNamesFromIterator (*iter);
      BOOST_CHECK (got == remaining);

      if (remaining.empty())
        break;

      if (start == remaining.front().first)
        remaining.pop_front();

      if (remaining.empty())
        start = end;
      else
        start = remaining.front().first;
    }
}

void
NameIterationTester::verify()
{
  verify (hybrid);

  /* Flush calls BatchWrite internally, and for that to work, we need to have
     a non-zero block hash.  Just set the block hash based on our counter.  */
  uint256 dummyBlockHash;
  *reinterpret_cast<unsigned*> (dummyBlockHash.begin()) = counter;
  hybrid.SetBestBlock (dummyBlockHash);

  hybrid.Flush();
  verify (db);
  verify (cache);
}

NameIterationTester::EntryList
NameIterationTester::getNamesFromView (const CCoinsView& view,
                                       const valtype& start)
{
  std::unique_ptr<CKevaIterator> iter(view.IterateKeys());
  iter->seek (start);

  return getNamesFromIterator (*iter);
}

NameIterationTester::EntryList
NameIterationTester::getNamesFromIterator (CKevaIterator& iter)
{
  EntryList res;

  valtype name;
  CNameData data;
  while (iter.next (name, data))
    res.push_back (std::make_pair (name, data));

  return res;
}

void
NameIterationTester::add (const std::string& n)
{
  const valtype& name = ValtypeFromString (n);
  const CNameData testData = getNextData();

  assert (data.count (name) == 0);
  data[name] = testData;
  hybrid.SetKeyValue (name, testData, false);
  cache.SetKeyValue (name, testData, false);
  verify();
}

void
NameIterationTester::update (const std::string& n)
{
  const valtype& name = ValtypeFromString (n);
  const CNameData testData = getNextData();

  assert (data.count (name) == 1);
  data[name] = testData;
  hybrid.SetKeyValue (name, testData, false);
  cache.SetKeyValue (name, testData, false);
  verify();
}

void
NameIterationTester::remove (const std::string& n)
{
  const valtype& name = ValtypeFromString (n);

  assert (data.count (name) == 1);
  data.erase (name);
  hybrid.DeleteKey (name);
  cache.DeleteKey (name);
  verify();
}

BOOST_AUTO_TEST_CASE (name_iteration)
{
  NameIterationTester tester(*pcoinsdbview);

  tester.verify();

  tester.add ("");
  tester.add ("a");
  tester.add ("aa");
  tester.add ("b");

  tester.remove ("aa");
  tester.remove ("b");
  tester.add ("b");
  tester.add ("aa");
  tester.remove ("b");
  tester.remove ("aa");

  tester.update ("");
  tester.add ("aa");
  tester.add ("b");
  tester.update ("b");
  tester.update ("aa");
}

/* ************************************************************************** */

/**
 * Construct a dummy tx that provides the given script as input
 * for further tests in the given CCoinsView.  The txid is returned
 * to refer to it.  The "index" is always 0.  The output's
 * value is always set to 1000 COIN, since it doesn't matter for
 * the tests we are interested in.
 * @param scr The script that should be provided as output.
 * @param nHeight The height of the coin.
 * @param view Add it to this view.
 * @return The out point for the newly added coin.
 */
static COutPoint
addTestCoin (const CScript& scr, unsigned nHeight, CCoinsViewCache& view)
{
  const CTxOut txout(1000 * COIN, scr);

  CMutableTransaction mtx;
  mtx.vout.push_back (txout);
  const CTransaction tx(mtx);

  Coin coin(txout, nHeight, false);
  const COutPoint outp(tx.GetHash(), 0);
  view.AddCoin (outp, std::move (coin), false);

  return outp;
}

BOOST_AUTO_TEST_CASE (name_tx_verification)
{
  const valtype name1 = ValtypeFromString ("test-name-1");
  const valtype name2 = ValtypeFromString ("test-name-2");
  const valtype value = ValtypeFromString ("my-value");

  const valtype tooLongName(256, 'x');
  const valtype tooLongValue(1024, 'x');

  const CScript addr = getTestAddress();

  const valtype rand(20, 'x');
  valtype toHash(rand);
  toHash.insert (toHash.end(), name1.begin(), name1.end());
  const uint160 hash = Hash160 (toHash);

  /* We use a basic coin view as standard situation for all the tests.
     Set it up with some basic input coins.  */

  CCoinsView dummyView;
  CCoinsViewCache view(&dummyView);

  const CScript scrNew = CNameScript::buildNameNew (addr, hash);
  const CScript scrFirst = CNameScript::buildNameFirstupdate (addr, name1,
                                                              value, rand);
  const CScript scrUpdate = CNameScript::buildNameUpdate (addr, name1, value);

  const COutPoint inCoin = addTestCoin (addr, 1, view);
  const COutPoint inNew = addTestCoin (scrNew, 100000, view);
  const COutPoint inFirst = addTestCoin (scrFirst, 100000, view);
  const COutPoint inUpdate = addTestCoin (scrUpdate, 100000, view);

  CNameData data1;
  data1.fromScript (100000, inFirst, CNameScript (scrFirst));
  view.SetKeyValue (name1, data1, false);

  /* ****************************************************** */
  /* Try out the Namecoin / non-Namecoin tx version check.  */

  CValidationState state;
  CMutableTransaction mtx;
  CScript scr;
  std::string reason;

  mtx.vin.push_back (CTxIn (inCoin));
  mtx.vout.push_back (CTxOut (COIN, addr));
  const CTransaction baseTx(mtx);

  /* Non-name tx should be non-Namecoin version.  */
  BOOST_CHECK (CheckNameTransaction (baseTx, 200000, view, state, 0));
  mtx.SetNamecoin();
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state, 0));

  /* Name tx should be Namecoin version.  */
  mtx = CMutableTransaction (baseTx);
  mtx.vin.push_back (CTxIn (inNew));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state, 0));
  mtx.SetNamecoin();
  mtx.vin.push_back (CTxIn (inUpdate));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state, 0));

  /* Duplicate name outs are not allowed.  */
  mtx = CMutableTransaction (baseTx);
  mtx.vout.push_back (CTxOut (COIN, scrNew));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state, 0));
  mtx.SetNamecoin();
  BOOST_CHECK (CheckNameTransaction (mtx, 200000, view, state, 0));
  mtx.vout.push_back (CTxOut (COIN, scrNew));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state, 0));

  /* ************************** */
  /* Test NAME_NEW validation.  */

  /* Basic verification of NAME_NEW.  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin();
  mtx.vout.push_back (CTxOut (COIN, scrNew));
  BOOST_CHECK (CheckNameTransaction (mtx, 200000, view, state, 0));
  mtx.vin.push_back (CTxIn (inNew));
  BOOST_CHECK (!CheckNameTransaction (mtx, 200000, view, state, 0));
  BOOST_CHECK (IsStandardTx (mtx, reason));

  /* Greedy names.  */
  mtx.vin.clear();
  mtx.vout[1].nValue = COIN / 100;
  BOOST_CHECK (CheckNameTransaction (mtx, 212500, view, state, 0));
  mtx.vout[1].nValue = COIN / 100 - 1;
  BOOST_CHECK (CheckNameTransaction (mtx, 212499, view, state, 0));
  BOOST_CHECK (!CheckNameTransaction (mtx, 212500, view, state, 0));

  /* ***************************** */
  /* Test NAME_UPDATE validation.  */

  /* Construct two versions of the coin view.  Since CheckNameTransaction
     verifies the name update against the name database, we have to ensure
     that it fits to the current test.  One version has the NAME_FIRSTUPDATE
     as previous state of the name, and one has the NAME_UPDATE.  */
  CCoinsViewCache viewFirst(&view);
  CCoinsViewCache viewUpd(&view);
  data1.fromScript (100000, inUpdate, CNameScript (scrUpdate));
  viewUpd.SetKeyValue (name1, data1, false);

  /* Check update of UPDATE output, plus expiry.  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin();
  mtx.vout.push_back (CTxOut (COIN, scrUpdate));
  BOOST_CHECK (!CheckNameTransaction (mtx, 135999, viewUpd, state, 0));
  mtx.vin.push_back (CTxIn (inUpdate));
  BOOST_CHECK (CheckNameTransaction (mtx, 135999, viewUpd, state, 0));
  BOOST_CHECK (!CheckNameTransaction (mtx, 136000, viewUpd, state, 0));
  BOOST_CHECK (IsStandardTx (mtx, reason));

  /* Check update of FIRSTUPDATE output, plus expiry.  */
  mtx.vin.clear();
  mtx.vin.push_back (CTxIn (inFirst));
  BOOST_CHECK (CheckNameTransaction (mtx, 135999, viewFirst, state, 0));
  BOOST_CHECK (!CheckNameTransaction (mtx, 136000, viewFirst, state, 0));

  /* No check for greedy names, since the test names are expired
     already at the greedy-name fork height.  Should not matter
     too much, though, as the checks are there for NAME_NEW
     and NAME_FIRSTUPDATE.  */

  /* Value length limits.  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin();
  mtx.vin.push_back (CTxIn (inUpdate));
  scr = CNameScript::buildNameUpdate (addr, name1, tooLongValue);
  mtx.vout.push_back (CTxOut (COIN, scr));
  BOOST_CHECK (!CheckNameTransaction (mtx, 110000, viewUpd, state, 0));

  /* Name mismatch to prev out.  */
  mtx.vout.clear();
  scr = CNameScript::buildNameUpdate (addr, name2, value);
  mtx.vout.push_back (CTxOut (COIN, scr));
  BOOST_CHECK (!CheckNameTransaction (mtx, 110000, viewUpd, state, 0));

  /* Previous NAME_NEW is not allowed!  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin();
  mtx.vout.push_back (CTxOut (COIN, scrUpdate));
  mtx.vin.push_back (CTxIn (inNew));
  CCoinsViewCache viewNew(&view);
  viewNew.DeleteKey (name1);
  BOOST_CHECK (!CheckNameTransaction (mtx, 110000, viewNew, state, 0));

  /* ********************************** */
  /* Test NAME_FIRSTUPDATE validation.  */

  CCoinsViewCache viewClean(&view);
  viewClean.DeleteKey (name1);

  /* Basic valid transaction.  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin();
  mtx.vout.push_back (CTxOut (COIN, scrFirst));
  BOOST_CHECK (!CheckNameTransaction (mtx, 100012, viewClean, state, 0));
  mtx.vin.push_back (CTxIn (inNew));
  BOOST_CHECK (CheckNameTransaction (mtx, 100012, viewClean, state, 0));
  BOOST_CHECK (IsStandardTx (mtx, reason));

  /* Maturity of prev out, acceptable for mempool.  */
  BOOST_CHECK (!CheckNameTransaction (mtx, 100011, viewClean, state, 0));
  BOOST_CHECK (CheckNameTransaction (mtx, 100011, viewClean, state,
                                     SCRIPT_VERIFY_NAMES_MEMPOOL));

  /* Expiry and re-registration of a name.  */
  BOOST_CHECK (!CheckNameTransaction (mtx, 135999, view, state, 0));
  BOOST_CHECK (CheckNameTransaction (mtx, 136000, view, state, 0));

  /* "Greedy" names.  */
  mtx.vout[1].nValue = COIN / 100;
  BOOST_CHECK (CheckNameTransaction (mtx, 212500, viewClean, state, 0));
  mtx.vout[1].nValue = COIN / 100 - 1;
  BOOST_CHECK (CheckNameTransaction (mtx, 212499, viewClean, state, 0));
  BOOST_CHECK (!CheckNameTransaction (mtx, 212500, viewClean, state, 0));

  /* Rand mismatch (wrong name activated).  */
  mtx.vout.clear();
  scr = CNameScript::buildNameFirstupdate (addr, name2, value, rand);
  BOOST_CHECK (!CheckNameTransaction (mtx, 100012, viewClean, state, 0));

  /* Non-NAME_NEW prev output.  */
  mtx = CMutableTransaction (baseTx);
  mtx.SetNamecoin();
  mtx.vout.push_back (CTxOut (COIN, scrFirst));
  mtx.vin.push_back (CTxIn (inUpdate));
  BOOST_CHECK (!CheckNameTransaction (mtx, 100012, viewClean, state, 0));
  mtx.vin.clear();
  mtx.vin.push_back (CTxIn (inFirst));
  BOOST_CHECK (!CheckNameTransaction (mtx, 100012, viewClean, state, 0));
}

#endif

/* ************************************************************************** */

BOOST_AUTO_TEST_CASE(keva_updates_undo)
{
  const valtype nameSpace = ValtypeFromString ("database-test-namespace");
  const valtype displayName = ValtypeFromString ("display name");
  const valtype key1 = ValtypeFromString ("key1");
  const valtype key2 = ValtypeFromString ("key2");
  const valtype value1_old = ValtypeFromString ("old-value_1");
  const valtype value1_new = ValtypeFromString ("new-value_1");
  const valtype value2_old = ValtypeFromString ("old-value_2");
  const valtype value2_new = ValtypeFromString ("new-value_2");
  const CScript addr = getTestAddress();

  CCoinsView dummyView;
  CCoinsViewCache view(&dummyView);
  CBlockUndo undo;
  CKevaData data;
  CKevaNotifier kevaNotifier(NULL);

  const CScript scrNew = CKevaScript::buildKevaNamespace(addr, nameSpace, displayName);
  const CScript scr1_1 = CKevaScript::buildKevaPut(addr, nameSpace, key1, value1_old);
  const CScript scr1_2 = CKevaScript::buildKevaPut(addr, nameSpace, key1, value1_new);
  const CScript scr2_1 = CKevaScript::buildKevaPut(addr, nameSpace, key2, value2_old);
  const CScript scr2_2 = CKevaScript::buildKevaPut(addr, nameSpace, key2, value2_new);


  /* The constructed tx needs not be valid.  We only test
     ApplyKevaTransaction and not validation.  */

  CBlockIndex pindex = CBlockIndex();
  CMutableTransaction mtx;
  mtx.SetKevacoin();
  mtx.vout.push_back(CTxOut(COIN, scrNew));
  pindex.nHeight = 100;
  ApplyKevaTransaction(mtx, pindex, view, undo, kevaNotifier);
  BOOST_CHECK(!view.GetName(nameSpace, key1, data));
  BOOST_CHECK(view.GetNamespace(nameSpace, data));
  BOOST_CHECK(undo.vkevaundo.size() == 1);

  mtx.vout.clear();
  mtx.vout.push_back(CTxOut(COIN, scr1_1));
  pindex.nHeight = 200;
  ApplyKevaTransaction(mtx, pindex, view, undo, kevaNotifier);
  BOOST_CHECK(view.GetName(nameSpace, key1, data));
  BOOST_CHECK(data.getHeight() == 200);
  BOOST_CHECK(data.getValue() == value1_old);
  BOOST_CHECK(data.getAddress() == addr);
  BOOST_CHECK(undo.vkevaundo.size() == 2);
  const CKevaData firstData = data;

  mtx.vout.clear();
  mtx.vout.push_back(CTxOut(COIN, scr1_2));
  pindex.nHeight = 300;
  ApplyKevaTransaction(mtx, pindex, view, undo, kevaNotifier);
  BOOST_CHECK(view.GetName(nameSpace, key1, data));
  BOOST_CHECK(data.getHeight() == 300);
  BOOST_CHECK(data.getValue() == value1_new);
  BOOST_CHECK(data.getAddress() == addr);
  BOOST_CHECK(undo.vkevaundo.size() == 3);

  undo.vkevaundo.back().apply(view);
  BOOST_CHECK(view.GetName(nameSpace, key1, data));
  BOOST_CHECK(data.getHeight() == 200);
  BOOST_CHECK(data.getValue() == value1_old);
  BOOST_CHECK(data.getAddress() == addr);
  undo.vkevaundo.pop_back();

  undo.vkevaundo.back().apply(view);
  BOOST_CHECK(!view.GetName(nameSpace, key1, data));
  BOOST_CHECK(view.GetNamespace(nameSpace, data));
  undo.vkevaundo.pop_back();

  undo.vkevaundo.back().apply(view);
  BOOST_CHECK(!view.GetNamespace(nameSpace, data));

  undo.vkevaundo.pop_back();
  BOOST_CHECK(undo.vkevaundo.empty());
}

/* ************************************************************************** */

BOOST_AUTO_TEST_CASE(keva_mempool)
{
  LOCK(mempool.cs);
  mempool.clear();

  const valtype nameSpace1 = ValtypeFromString ("namespace-dummy-1");
  const valtype nameSpace2 = ValtypeFromString ("namespace-dummy-2");
  const valtype displayName = ValtypeFromString ("display name");
  const valtype keyA = ValtypeFromString ("key-a");
  const valtype keyB = ValtypeFromString ("key-b");
  const valtype valueA = ValtypeFromString ("value-a");
  const valtype valueB = ValtypeFromString ("value-b");
  const CScript addr = getTestAddress();

  const CScript addr2 = (CScript (addr) << OP_RETURN);

  const CScript new1 = CKevaScript::buildKevaNamespace(addr, nameSpace1, displayName);
  const CScript new2 = CKevaScript::buildKevaNamespace(addr, nameSpace2, displayName);
  const CScript upd1 = CKevaScript::buildKevaPut(addr, nameSpace1, keyA, valueA);
  const CScript upd2 = CKevaScript::buildKevaPut(addr, nameSpace2, keyB, valueB);

  /* The constructed tx needs not be valid.  We only test
     the mempool acceptance and not validation.  */

  CMutableTransaction txNew1;
  txNew1.SetKevacoin();
  txNew1.vout.push_back(CTxOut(COIN, new1));
  CMutableTransaction txNew2;
  txNew2.SetKevacoin();
  txNew2.vout.push_back(CTxOut(COIN, new2));

  CMutableTransaction txUpd1;
  txUpd1.SetKevacoin();
  txUpd1.vout.push_back(CTxOut(COIN, upd1));
  CMutableTransaction txUpd2;
  txUpd2.SetKevacoin();
  txUpd2.vout.push_back(CTxOut(COIN, upd2));

  /* Build an invalid transaction.  It should not crash (assert fail)
     the mempool check.  */

  CMutableTransaction txInvalid;
  txInvalid.SetKevacoin();
  mempool.checkKevaOps(txInvalid);

  txInvalid.vout.push_back(CTxOut (COIN, new1));
  txInvalid.vout.push_back(CTxOut (COIN, new2));
  txInvalid.vout.push_back(CTxOut (COIN, upd1));
  txInvalid.vout.push_back(CTxOut (COIN, upd2));
  mempool.checkKevaOps(txInvalid);

  /* For an empty mempool, all tx should be fine.  */
  std::vector<std::tuple<valtype, valtype, uint256>> unconfirmedNamespace;
  mempool.getUnconfirmedNamespaceList(unconfirmedNamespace);
  BOOST_CHECK(unconfirmedNamespace.size() == 0);

  /* Add a name registration.  */
  const LockPoints lp;
  CTxMemPoolEntry entryReg(MakeTransactionRef(txNew1), 0, 0, 100,
                                 false, 1, lp);
  BOOST_CHECK(entryReg.isNamespaceRegistration() && !entryReg.isKeyUpdate());
  BOOST_CHECK(entryReg.getNamespace() == nameSpace1);
  mempool.addUnchecked(entryReg.GetTx().GetHash(), entryReg);
  mempool.addKevaUnchecked(entryReg.GetTx().GetHash(), entryReg.GetKevaOp());
  mempool.getUnconfirmedNamespaceList(unconfirmedNamespace);
  BOOST_CHECK(unconfirmedNamespace.size() == 1);
  BOOST_CHECK(std::get<0>(unconfirmedNamespace[0]) == nameSpace1);

  /* Add a name update.  */
  CTxMemPoolEntry entryUpd(MakeTransactionRef(txUpd1), 0, 0, 100,
                                 false, 1, lp);
  BOOST_CHECK(!entryUpd.isNamespaceRegistration() && entryUpd.isKeyUpdate());
  BOOST_CHECK(entryUpd.getNamespace() == nameSpace1);
  mempool.addUnchecked (entryUpd.GetTx().GetHash(), entryUpd);
  mempool.addKevaUnchecked(entryUpd.GetTx().GetHash(), entryUpd.GetKevaOp());
  valtype valResult;
  mempool.getUnconfirmedKeyValue(nameSpace1, keyA, valResult);
  BOOST_CHECK(valResult == valueA);
  std::vector<std::tuple<valtype, valtype, valtype, uint256>> keyValueList;
  mempool.getUnconfirmedKeyValueList(keyValueList, nameSpace1);
  BOOST_CHECK(keyValueList.size() == 1);

  /* Run mempool sanity check.  */
#if 0
  CCoinsViewCache view(pcoinsTip.get());
  const CKevaScript nameOp(upd1);
  CKevaData data;
  data.fromScript(100, COutPoint (uint256(), 0), nameOp);
  view.SetKeyValue(nameUpd, data, false);
  mempool.checkNames(&view);
#endif

  /* Remove the transactions again.  */
  mempool.removeRecursive(txNew1);
  std::vector<std::tuple<valtype, valtype, uint256>> unconfirmedNS;
  mempool.getUnconfirmedNamespaceList(unconfirmedNS);
  BOOST_CHECK(unconfirmedNS.size() == 0);
#if 0
  mempool.removeRecursive(txUpd1);
  BOOST_CHECK(!mempool.updatesName (nameUpd));
  BOOST_CHECK(mempool.checkNameOps (txUpd1) && mempool.checkNameOps (txUpd2));
  BOOST_CHECK(mempool.checkNameOps (txReg1));

  mempool.removeRecursive (txNew1);
  mempool.removeRecursive (txNew2);
  BOOST_CHECK(!mempool.checkNameOps (txNew1p));
  BOOST_CHECK(mempool.checkNameOps (txNew1) && mempool.checkNameOps (txNew2));

  /* Check getTxForName with non-existent names.  */
  BOOST_CHECK(mempool.getTxForName (nameReg).IsNull());
  BOOST_CHECK(mempool.getTxForName (nameUpd).IsNull());

  /* Check removing of conflicted name registrations.  */

  mempool.addUnchecked (entryReg.GetTx().GetHash(), entryReg);
  BOOST_CHECK(mempool.registersName (nameReg));
  BOOST_CHECK(!mempool.checkNameOps (txReg2));

  {
    CNameConflictTracker tracker(mempool);
    mempool.removeConflicts (txReg2);
    BOOST_CHECK(tracker.GetNameConflicts()->size() == 1);
    BOOST_CHECK(tracker.GetNameConflicts()->front()->GetHash()
                  == txReg1.GetHash());
  }
  BOOST_CHECK(!mempool.registersName (nameReg));
  BOOST_CHECK(mempool.mapTx.empty());

  /* Check removing of conflicts after name expiration.  */

  mempool.addUnchecked (entryUpd.GetTx().GetHash(), entryUpd);
  BOOST_CHECK(mempool.updatesName (nameUpd));
  BOOST_CHECK(!mempool.checkNameOps (txUpd2));

  std::set<valtype> names;
  names.insert(nameUpd);
  {
    CNameConflictTracker tracker(mempool);
    mempool.removeExpireConflicts(names);
    BOOST_CHECK(tracker.GetNameConflicts()->size() == 1);
    BOOST_CHECK(tracker.GetNameConflicts()->front()->GetHash()
                  == txUpd1.GetHash());
  }
  BOOST_CHECK(!mempool.updatesName (nameUpd));
  BOOST_CHECK(mempool.mapTx.empty());

  /* Check removing of conflicts after name unexpiration.  */

  mempool.addUnchecked (entryReg.GetTx().GetHash(), entryReg);
  BOOST_CHECK(mempool.registersName (nameReg));
  BOOST_CHECK(!mempool.checkNameOps (txReg2));

  names.clear();
  names.insert(nameReg);
  {
    CNameConflictTracker tracker(mempool);
    mempool.removeUnexpireConflicts(names);
    BOOST_CHECK(tracker.GetNameConflicts()->size() == 1);
    BOOST_CHECK(tracker.GetNameConflicts()->front()->GetHash()
                  == txReg1.GetHash());
  }
  BOOST_CHECK(!mempool.registersName (nameReg));
  BOOST_CHECK(mempool.mapTx.empty());
#endif
}

/* ************************************************************************** */

BOOST_AUTO_TEST_SUITE_END()
