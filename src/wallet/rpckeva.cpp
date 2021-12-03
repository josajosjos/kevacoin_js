// Copyright (c) 2014-2017 Daniel Kraft
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Copyright (c) 2018 the Kevacoin Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "coins.h"
#include "init.h"
#include "keva/common.h"
#include "keva/main.h"
#include "primitives/transaction.h"
#include "random.h"
#include "rpc/mining.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "script/keva.h"
#include "txmempool.h"
#include "util.h"
#include "validation.h"
#include "wallet/coincontrol.h"
#include "wallet/wallet.h"

#include <univalue.h>
#include <boost/xpressive/xpressive_dynamic.hpp>

const int NAMESPACE_LENGTH           =  21;
const std::string DUMMY_NAMESPACE    =  "___DUMMY_NAMESPACE___";


/* ************************************************************************** */

UniValue keva_namespace(const JSONRPCRequest& request)
{
  CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
  if (!EnsureWalletIsAvailable (pwallet, request.fHelp))
    return NullUniValue;

  if (request.fHelp || request.params.size() != 1)
    throw std::runtime_error (
        "keva_namespace \"display_name\"\n"
        "\nRegister a namespace with the given display name.\n"
        + HelpRequiringPassphrase (pwallet) +
        "\nArguments:\n"
        "1. \"display_name\"          (string, required) the display name of the namespace\n"
        "\nResult:\n"
        "[\n"
        "  xxxxx,   (string) the txid\n"
        "  xxxxx,   (string) the unique namespace id\n"
        "]\n"
        "\nExamples:\n"
        + HelpExampleCli ("keva_namespace", "\"display name\"")
      );

  RPCTypeCheck (request.params, {UniValue::VSTR});

  ObserveSafeMode ();

  const std::string displayNameStr = request.params[0].get_str();
  const valtype displayName = ValtypeFromString (displayNameStr);
  if (displayName.size() > MAX_NAMESPACE_LENGTH) {
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the name is too long");
  }

  /* No explicit locking should be necessary.  CReserveKey takes care
     of locking the wallet, and CommitTransaction (called when sending
     the tx) locks cs_main as necessary.  */

  EnsureWalletIsUnlocked (pwallet);

  CReserveKey keyName(pwallet);
  CPubKey pubKey;
  const bool ok = keyName.GetReservedKey(pubKey, true);
  assert (ok);

  CKeyID keyId = pubKey.GetID();

  // The namespace name is: Hash160("first txin")
  // For now we don't know the first txin, so use dummy name here.
  // It will be replaced later in CreateTransaction.
  valtype namespaceDummy = ToByteVector(std::string(DUMMY_NAMESPACE));
  assert(namespaceDummy.size() == NAMESPACE_LENGTH);

  CScript redeemScript = GetScriptForDestination(WitnessV0KeyHash(keyId));
  CScriptID scriptHash = CScriptID(redeemScript);
  CScript addrName = GetScriptForDestination(scriptHash);
  const CScript newScript = CKevaScript::buildKevaNamespace(addrName, namespaceDummy, displayName);

  CCoinControl coinControl;
  CWalletTx wtx;
  valtype kevaNamespace;
  SendMoneyToScript(pwallet, newScript, nullptr, kevaNamespace,
                     KEVA_LOCKED_AMOUNT, false, wtx, coinControl);
  keyName.KeepKey();

  std::string kevaNamespaceBase58 = EncodeBase58Check(kevaNamespace);
  const std::string txid = wtx.GetHash().GetHex();
  LogPrintf("keva_namespace: namespace=%s, displayName=%s, tx=%s\n",
             kevaNamespaceBase58.c_str(), displayNameStr.c_str(), txid.c_str());

  UniValue obj(UniValue::VOBJ);
  obj.pushKV("txid", txid);
  obj.pushKV("namespaceId", kevaNamespaceBase58);
  return obj;
}

UniValue keva_list_namespaces(const JSONRPCRequest& request)
{
  CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
  if (!EnsureWalletIsAvailable (pwallet, request.fHelp))
    return NullUniValue;

  if (request.fHelp)
    throw std::runtime_error (
        "keva_list_namespaces\n"
        "\nList all namespaces.\n"
        + HelpRequiringPassphrase(pwallet) +
        "\nArguments:\n"
        "\nResult:\n"
        "[\n"
        "  { namespaceId: <namespce Id>, displayName: <display name> }\n"
        "  ...\n"
        "]\n"
        "\nExamples:\n"
        + HelpExampleCli("keva_list_namespaces", "")
      );

  RPCTypeCheck (request.params, {UniValue::VSTR});

  ObserveSafeMode ();

  std::map<std::string, std::string> mapObjects;
  {
    LOCK2 (cs_main, pwallet->cs_wallet);
    std::set<CTxDestination> destinations;
    std::vector<COutput> vecOutputs;
    bool include_unsafe = true;
    CAmount nMinimumAmount = 0;
    CAmount nMaximumAmount = MAX_MONEY;
    CAmount nMinimumSumAmount = MAX_MONEY;
    uint64_t nMaximumCount = 0;
    int nMinDepth = 1;
    int nMaxDepth = 99999999;
    pwallet->AvailableCoins(vecOutputs, !include_unsafe, nullptr, nMinimumAmount, nMaximumAmount, nMinimumSumAmount, nMaximumCount, nMinDepth, nMaxDepth);
    for (const COutput& out : vecOutputs) {
      CTxDestination address;
      const CScript& scriptPubKey = out.tx->tx->vout[out.i].scriptPubKey;
      const CKevaScript kevaOp(scriptPubKey);
      if (!kevaOp.isKevaOp()) {
        continue;
      }
      if (!kevaOp.isNamespaceRegistration() && !kevaOp.isAnyUpdate()) {
        continue;
      }
      const valtype nameSpace = kevaOp.getOpNamespace();
      const std::string nameSpaceStr = EncodeBase58Check(nameSpace);
      CKevaData data;
      if (pcoinsTip->GetNamespace(nameSpace, data)) {
        std::string displayName = ValtypeToString(data.getValue());
        mapObjects[nameSpaceStr] = displayName;
      }
    }
  }

  UniValue res(UniValue::VARR);
  for (const auto& item : mapObjects) {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("namespaceId", item.first);
    obj.pushKV("displayName", item.second);
    res.push_back(obj);
  }

  {
    LOCK (mempool.cs);
    // Also get the unconfirmed ones from mempool.
    std::vector<std::tuple<valtype, valtype, uint256>> unconfirmedNamespaces;
    mempool.getUnconfirmedNamespaceList(unconfirmedNamespaces);
    for (auto entry : unconfirmedNamespaces) {
      UniValue obj(UniValue::VOBJ);
      obj.pushKV("namespaceId", EncodeBase58Check(std::get<0>(entry)));
      obj.pushKV("displayName", ValtypeToString(std::get<1>(entry)));
      res.push_back(obj);
    }
  }

  return res;
}

UniValue keva_put(const JSONRPCRequest& request)
{
  CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
  if (!EnsureWalletIsAvailable (pwallet, request.fHelp)) {
    return NullUniValue;
  }

  if (request.fHelp || request.params.size() < 3 || request.params.size() > 4) {
    throw std::runtime_error (
        "keva_put \"namespace\" \"key\" \"value\" \"address\" \n"
        "\nInsert or update a key value pair in the given namespace.\n"
        + HelpRequiringPassphrase (pwallet) +
        "\nArguments:\n"
        "1. \"namespace\"            (string, required) the namespace to insert the key to\n"
        "2. \"key\"                  (string, required) value for the key\n"
        "3. \"value\"                (string, required) value for the name\n"
        "4. \"address\"              (string, optional) transfer the namespace to the given address\n"
        "\nResult:\n"
        "\"txid\"             (string) the keva_put's txid\n"
        "\nExamples:\n"
        + HelpExampleCli ("keva_put", "\"mynamespace\", \"new-key\", \"new-value\"")
      );
  }

  RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR});
  RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
  RPCTypeCheckArgument(request.params[1], UniValue::VSTR);
  RPCTypeCheckArgument(request.params[2], UniValue::VSTR);

  bool hasAddress = false;
  if (request.params.size() == 4) {
    RPCTypeCheckArgument(request.params[3], UniValue::VSTR);
    hasAddress = true;
  }

  ObserveSafeMode ();

  const std::string namespaceStr = request.params[0].get_str();
  valtype nameSpace;
  if (!DecodeKevaNamespace(namespaceStr, Params(), nameSpace)) {
    throw JSONRPCError (RPC_INVALID_PARAMETER, "invalid namespace id");
  }
  if (nameSpace.size() > MAX_NAMESPACE_LENGTH)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the namespace is too long");

  const std::string keyStr = request.params[1].get_str ();
  const valtype key = ValtypeFromString (keyStr);
  if (key.size() > MAX_KEY_LENGTH)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the key is too long");

  const std::string valueStr = request.params[2].get_str ();
  const valtype value = ValtypeFromString (valueStr);
  if (value.size() > MAX_VALUE_LENGTH)
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the value is too long");

  EnsureWalletIsUnlocked(pwallet);

  COutput output;
  std::string kevaNamespce = namespaceStr;
  if (!pwallet->FindKevaCoin(output, kevaNamespce)) {
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "this namespace can not be updated");
  }
  const COutPoint outp(output.tx->GetHash(), output.i);
  const CTxIn txIn(outp);

  CReserveKey keyName(pwallet);
  CScript addrName;
  if (hasAddress) {
    CTxDestination dest = DecodeDestination(request.params[3].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    addrName = GetScriptForDestination(dest);
  } else {
    CPubKey pubKeyReserve;
    const bool ok = keyName.GetReservedKey(pubKeyReserve, true);
    assert(ok);
    CKeyID keyId = pubKeyReserve.GetID();
    CScript redeemScript = GetScriptForDestination(WitnessV0KeyHash(keyId));
    CScriptID scriptHash = CScriptID(redeemScript);
    addrName = GetScriptForDestination(scriptHash);
  }

  const CScript kevaScript = CKevaScript::buildKevaPut(addrName, nameSpace, key, value);

  CCoinControl coinControl;
  CWalletTx wtx;
  valtype empty;
  SendMoneyToScript(pwallet, kevaScript, &txIn, empty,
                     KEVA_LOCKED_AMOUNT, false, wtx, coinControl);

  if (!hasAddress) {
    keyName.KeepKey();
  }
  UniValue obj(UniValue::VOBJ);
  obj.pushKV("txid", wtx.GetHash().GetHex());
  return obj;
}

UniValue keva_delete(const JSONRPCRequest& request)
{
  CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
  if (!EnsureWalletIsAvailable (pwallet, request.fHelp)) {
    return NullUniValue;
  }

  if (request.fHelp || request.params.size() != 2) {
    throw std::runtime_error (
        "keva_delete \"namespace\" \"key\"\n"
        "\nRemove the specified key from the given namespace.\n"
        + HelpRequiringPassphrase (pwallet) +
        "\nArguments:\n"
        "1. \"namespace\"            (string, required) the namespace to insert the key to\n"
        "2. \"key\"                  (string, required) value for the key\n"
        "\nResult:\n"
        "\"txid\"             (string) the keva_delete's txid\n"
        "\nExamples:\n"
        + HelpExampleCli ("keva_delete", "\"mynamespace\", \"key\"")
      );
  }

  RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR});
  RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
  RPCTypeCheckArgument(request.params[1], UniValue::VSTR);

  ObserveSafeMode ();

  const std::string namespaceStr = request.params[0].get_str();
  valtype nameSpace;
  if (!DecodeKevaNamespace(namespaceStr, Params(), nameSpace)) {
    throw JSONRPCError (RPC_INVALID_PARAMETER, "invalid namespace id");
  }
  if (nameSpace.size() > MAX_NAMESPACE_LENGTH) {
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the namespace is too long");
  }

  const std::string keyStr = request.params[1].get_str ();
  const valtype key = ValtypeFromString (keyStr);
  if (key.size() > MAX_KEY_LENGTH) {
    throw JSONRPCError (RPC_INVALID_PARAMETER, "the key is too long");
  }

  if (keyStr == CKevaScript::KEVA_DISPLAY_NAME_KEY) {
    throw JSONRPCError (RPC_INVALID_PARAMETER, "_KEVA_NS_ cannot be deleted");
  }

  bool hasKey = false;
  CKevaData data;
  {
    LOCK2(cs_main, mempool.cs);
    std::vector<std::tuple<valtype, valtype, valtype, uint256>> unconfirmedKeyValueList;
    valtype val;
    if (mempool.getUnconfirmedKeyValue(nameSpace, key, val)) {
      if (val.size() > 0) {
        hasKey = true;
      }
    } else if (pcoinsTip->GetName(nameSpace, key, data)) {
      hasKey = true;
    }
  }

  if (!hasKey) {
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "key not found");
  }

  EnsureWalletIsUnlocked(pwallet);

  COutput output;
  std::string kevaNamespce = namespaceStr;
  if (!pwallet->FindKevaCoin(output, kevaNamespce)) {
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "this namespace can not be updated");
  }
  const COutPoint outp(output.tx->GetHash(), output.i);
  const CTxIn txIn(outp);

  CReserveKey keyName(pwallet);
  CPubKey pubKeyReserve;
  const bool ok = keyName.GetReservedKey(pubKeyReserve, true);
  assert(ok);

  CScript redeemScript = GetScriptForDestination(WitnessV0KeyHash(pubKeyReserve.GetID()));
  CScriptID scriptHash = CScriptID(redeemScript);
  CScript addrName = GetScriptForDestination(scriptHash);

  const CScript kevaScript = CKevaScript::buildKevaDelete(addrName, nameSpace, key);

  CCoinControl coinControl;
  CWalletTx wtx;
  valtype empty;
  SendMoneyToScript(pwallet, kevaScript, &txIn, empty,
                     KEVA_LOCKED_AMOUNT, false, wtx, coinControl);

  keyName.KeepKey();

  UniValue obj(UniValue::VOBJ);
  obj.pushKV("txid", wtx.GetHash().GetHex());
  return obj;
}

UniValue keva_pending(const JSONRPCRequest& request)
{
  if (request.fHelp || request.params.size() > 1)
    throw std::runtime_error (
        "keva_pending (\"namespace\")\n"
        "\nList unconfirmed keva operations in the mempool.\n"
        "\nIf a namespace is given, only check for operations on this namespace.\n"
        "\nArguments:\n"
        "1. \"namespace\"        (string, optional) only look for this namespace\n"
        "\nResult:\n"
        "[\n"
        "  {\n"
        "    \"op\": xxxx       (string) the operation being performed\n"
        "    \"name\": xxxx     (string) the namespace operated on\n"
        "    \"value\": xxxx    (string) the namespace's new value\n"
        "    \"txid\": xxxx     (string) the txid corresponding to the operation\n"
        "    \"ismine\": xxxx   (boolean) whether the name is owned by the wallet\n"
        "  },\n"
        "  ...\n"
        "]\n"
        + HelpExampleCli ("keva_pending", "")
        + HelpExampleCli ("keva_pending", "\"NJdZVkeerpgxqFM2oAzitVU8xsdj7\"")
        + HelpExampleRpc ("keva_pending", "")
      );

  ObserveSafeMode ();

  std::string namespaceStr;
  valtype nameSpace;

  if (request.params.size() == 1) {
    RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
    namespaceStr = request.params[0].get_str();
    if (!DecodeKevaNamespace(namespaceStr, Params(), nameSpace)) {
      throw JSONRPCError (RPC_INVALID_PARAMETER, "invalid namespace id");
    }
  }

  LOCK (mempool.cs);

  std::vector<std::tuple<valtype, valtype, uint256>> unconfirmedNamespaces;
  mempool.getUnconfirmedNamespaceList(unconfirmedNamespaces);

  std::vector<std::tuple<valtype, valtype, valtype, uint256>> unconfirmedKeyValueList;
  mempool.getUnconfirmedKeyValueList(unconfirmedKeyValueList, nameSpace);

  UniValue arr(UniValue::VARR);
  const std::string opKevaNamepsace = "keva_namespace";
  const std::string opKevaPut       = "keva_put";
  const std::string opKevaDelete    = "keva_delete";

  for (auto entry: unconfirmedNamespaces) {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("op", opKevaNamepsace);
    obj.pushKV("namespace", EncodeBase58Check(std::get<0>(entry)));
    obj.pushKV("display name", ValtypeToString(std::get<1>(entry)));
    obj.pushKV("txid", std::get<2>(entry).ToString());
    arr.push_back(obj);
  }

  for (auto entry: unconfirmedKeyValueList) {
    UniValue obj(UniValue::VOBJ);
    const valtype val = std::get<2>(entry);
    if (val.size() > 0) {
      obj.pushKV("op", opKevaPut);
      obj.pushKV("namespace", EncodeBase58Check(std::get<0>(entry)));
      obj.pushKV("key", ValtypeToString(std::get<1>(entry)));
      obj.pushKV("value", ValtypeToString(std::get<2>(entry)));
      obj.pushKV("txid", std::get<3>(entry).ToString());
    } else {
      obj.pushKV("op", opKevaDelete);
      obj.pushKV("namespace", EncodeBase58Check(std::get<0>(entry)));
      obj.pushKV("key", ValtypeToString(std::get<1>(entry)));
      obj.pushKV("txid", std::get<3>(entry).ToString());
    }
    arr.push_back(obj);
  }

  return arr;
}

UniValue keva_group_join(const JSONRPCRequest& request)
{
  CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
  if (!EnsureWalletIsAvailable (pwallet, request.fHelp)) {
    return NullUniValue;
  }

  if (request.fHelp || request.params.size() != 2) {
    throw std::runtime_error (
        "keva_group_join \"my_namespace\" \"other_namespace\"\n"
        "\nJoin the other namespace, so that the data in both namespaces can be combined. See keva_group_leave.\n"
        + HelpRequiringPassphrase (pwallet) +
        "\nArguments:\n"
        "1. \"my_namespace\"         (string, required) the namespace to join to <other_namespace>\n"
        "2. \"other_namespace\"      (string, required) the target namespace to join to\n"
        "\nResult:\n"
        "\"txid\"             (string) the keva_put's txid\n"
        "\nExamples:\n"
        + HelpExampleCli ("keva_group_join", "\"my_namespace\", \"other_namespace\"")
      );
  }

  RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR});
  RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
  RPCTypeCheckArgument(request.params[1], UniValue::VSTR);

  ObserveSafeMode ();

  const std::string myNamespaceStr = request.params[0].get_str();
  valtype myNamespace;
  if (!DecodeKevaNamespace(myNamespaceStr, Params(), myNamespace)) {
    throw JSONRPCError (RPC_INVALID_PARAMETER, "invalid namespace id for my_namespace");
  }

  const std::string otherNamespaceStr = request.params[1].get_str();
  valtype otherNamespace;
  if (!DecodeKevaNamespace(otherNamespaceStr, Params(), otherNamespace)) {
    throw JSONRPCError (RPC_INVALID_PARAMETER, "invalid namespace id for other_namespace");
  }

  // Check other_namespace exists.
  {
    LOCK(cs_main);
    CKevaData data;
    if (!pcoinsTip->GetName(otherNamespace, ValtypeFromString(CKevaScript::KEVA_DISPLAY_NAME_KEY), data)) {
      throw JSONRPCError (RPC_INVALID_PARAMETER, "other_namespace does not exist");
    }
  }

  EnsureWalletIsUnlocked(pwallet);

  COutput output;
  if (!pwallet->FindKevaCoin(output, myNamespaceStr)) {
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "this namespace can not be updated");
  }
  const COutPoint outp(output.tx->GetHash(), output.i);
  const CTxIn txIn(outp);

  CReserveKey keyName(pwallet);
  CPubKey pubKeyReserve;
  const bool ok = keyName.GetReservedKey(pubKeyReserve, true);
  assert(ok);
  CKeyID keyId = pubKeyReserve.GetID();
  CScript redeemScript = GetScriptForDestination(WitnessV0KeyHash(keyId));
  CScriptID scriptHash = CScriptID(redeemScript);
  CScript addrName = GetScriptForDestination(scriptHash);

  valtype dummy = ValtypeFromString(std::string("1"));
  valtype key = ValtypeFromString(CKevaData::ASSOCIATE_PREFIX + otherNamespaceStr);
  const CScript kevaScript = CKevaScript::buildKevaPut(addrName, myNamespace, key, dummy);

  CCoinControl coinControl;
  CWalletTx wtx;
  valtype empty;
  SendMoneyToScript(pwallet, kevaScript, &txIn, empty,
                     KEVA_LOCKED_AMOUNT, false, wtx, coinControl);

  keyName.KeepKey();
  UniValue obj(UniValue::VOBJ);
  obj.pushKV("txid", wtx.GetHash().GetHex());
  return obj;
}

UniValue keva_group_leave(const JSONRPCRequest& request)
{
  CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
  if (!EnsureWalletIsAvailable (pwallet, request.fHelp)) {
    return NullUniValue;
  }

  if (request.fHelp || request.params.size() != 2) {
    throw std::runtime_error (
        "keva_group_leave \"my_namespace\" \"other_namespace\"\n"
        "\nLeave the other namespace so that the data are not to be combined with it. See keva_group_join.\n"
        + HelpRequiringPassphrase (pwallet) +
        "\nArguments:\n"
        "1. \"my_namespace\"         (string, required) the namespace to leave <other_namespace>\n"
        "2. \"other_namespace\"      (string, required) the target namespace to leave\n"
        "\nResult:\n"
        "\"txid\"             (string) the keva_put's txid\n"
        "\nExamples:\n"
        + HelpExampleCli ("keva_group_leave", "\"my_namespace\", \"other_namespace\"")
      );
  }

  RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR});
  RPCTypeCheckArgument(request.params[0], UniValue::VSTR);
  RPCTypeCheckArgument(request.params[1], UniValue::VSTR);

  ObserveSafeMode ();

  const std::string myNamespaceStr = request.params[0].get_str();
  valtype myNamespace;
  if (!DecodeKevaNamespace(myNamespaceStr, Params(), myNamespace)) {
    throw JSONRPCError (RPC_INVALID_PARAMETER, "invalid namespace id for my_namespace");
  }

  const std::string otherNamespaceStr = request.params[1].get_str();
  valtype otherNamespace;
  if (!DecodeKevaNamespace(otherNamespaceStr, Params(), otherNamespace)) {
    throw JSONRPCError (RPC_INVALID_PARAMETER, "invalid namespace id for other_namespace");
  }

  // Check other_namespace exists.
  {
    LOCK(cs_main);
    CKevaData data;
    if (!pcoinsTip->GetName(otherNamespace, ValtypeFromString(CKevaScript::KEVA_DISPLAY_NAME_KEY), data)) {
      throw JSONRPCError (RPC_INVALID_PARAMETER, "other_namespace does not exist");
    }
  }

  EnsureWalletIsUnlocked(pwallet);

  COutput output;
  if (!pwallet->FindKevaCoin(output, myNamespaceStr)) {
    throw JSONRPCError (RPC_TRANSACTION_ERROR, "this namespace can not be updated");
  }
  const COutPoint outp(output.tx->GetHash(), output.i);
  const CTxIn txIn(outp);

  CReserveKey keyName(pwallet);
  CPubKey pubKeyReserve;
  const bool ok = keyName.GetReservedKey(pubKeyReserve, true);
  assert(ok);
  CKeyID keyId = pubKeyReserve.GetID();

  CScript redeemScript = GetScriptForDestination(WitnessV0KeyHash(pubKeyReserve.GetID()));
  CScriptID scriptHash = CScriptID(redeemScript);
  CScript addrName = GetScriptForDestination(scriptHash);

  valtype key = ValtypeFromString(CKevaData::ASSOCIATE_PREFIX + otherNamespaceStr);
  const CScript kevaScript = CKevaScript::buildKevaDelete(addrName, myNamespace, key);

  CCoinControl coinControl;
  CWalletTx wtx;
  valtype empty;
  SendMoneyToScript(pwallet, kevaScript, &txIn, empty,
                     KEVA_LOCKED_AMOUNT, false, wtx, coinControl);

  keyName.KeepKey();
  UniValue obj(UniValue::VOBJ);
  obj.pushKV("txid", wtx.GetHash().GetHex());
  return obj;
}
