/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file ParallelConfigPrecompiled.cpp
 *  @author jimmyshi
 *  @date 20190315
 */

#include "ParallelConfigPrecompiled.h"
#include <libstorage/EntriesPrecompiled.h>
#include <libstorage/TableFactoryPrecompiled.h>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::storage;
using namespace dev::precompiled;
using namespace dev::blockverifier;

/*
    table name: __contract_parallel_func_CONTRACTADDR_
    | key      | index | selector   | functionName                    | criticalSize |
    | -------- | ----- | ---------- | ------------------------------- | ------------ |
    | parallel | 0     | 0x12345678 | transfer(string,string,uint256) | 2            |
    | parallel | 1     | 0x23456789 | set(string,uint256)             | 1            |
*/

const string PARA_KEY = "parallel";
const string PARA_SELECTOR = "selector";
const string PARA_FUNC_NAME = "functionName";
const string PARA_CRITICAL_SIZE = "criticalSize";

const string PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT =
    "registerParallelFunctionInternal(address,string,uint256)";
const string PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR =
    "unregisterParallelFunctionInternal(address,string)";

const string PARA_KEY_NAME = PARA_KEY;
const string PARA_VALUE_NAMES = PARA_SELECTOR + "," + PARA_FUNC_NAME + "," + PARA_CRITICAL_SIZE;


ParallelConfigPrecompiled::ParallelConfigPrecompiled()
{
    name2Selector[PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT] =
        getFuncSelector(PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT);
    name2Selector[PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR] =
        getFuncSelector(PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR);
}

string ParallelConfigPrecompiled::toString()
{
    return "ParallelConfig";
}

bytes ParallelConfigPrecompiled::call(
    dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    ContractABI abi;
    bytes out;

    if (func == name2Selector[PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT])
    {
        registerParallelFunction(context, data, origin, out);
    }
    else if (func == name2Selector[PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR])
    {
        unregisterParallelFunction(context, data, origin, out);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ParallelConfigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    return out;
}

Table::Ptr ParallelConfigPrecompiled::openTable(dev::blockverifier::ExecutiveContext::Ptr context,
    Address const& contractAddress, Address const& origin, bool needCreate)
{
    string tableName = "_parallel_func_" + contractAddress.hex() + "_";
    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
        dynamic_pointer_cast<TableFactoryPrecompiled>(context->getPrecompiled(Address(0x1001)));
    if (!tableFactoryPrecompiled)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ParallelConfigPrecompiled")
                               << LOG_DESC("TableFactoryPrecompiled has not been initrailized");
        return nullptr;
    }

    auto table =
        tableFactoryPrecompiled->getMemoryTableFactory()->openTable(tableName, false, false);

    if (!table && needCreate)
    {  //__dat_transfer__ is not exist, then create it first.
        table = tableFactoryPrecompiled->getMemoryTableFactory()->createTable(
            tableName, PARA_KEY_NAME, PARA_VALUE_NAMES, false, origin, false);

        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ParallelConfigPrecompiled") << LOG_DESC("open table")
                               << LOG_DESC(" create parallel config table. ")
                               << LOG_KV("tableName", tableName);
    }

    return table;
}

void ParallelConfigPrecompiled::registerParallelFunction(
    dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef data, Address const& origin,
    bytes& out)
{
    // registerParallelFunctionInternal(address,string,uint256)
    // registerParallelFunctionInternal(address contractAddress, string functionName, uint256
    // criticalSize)

    Address contractAddress;
    string functionName;
    u256 criticalSize;

    ContractABI abi;
    abi.abiOut(data, contractAddress, functionName, criticalSize);
    uint32_t selector = getFuncSelector(functionName);

    Table::Ptr table = openTable(context, contractAddress, origin);
    if (table && table.get())
    {
        Entry::Ptr entry = table->newEntry();
        entry->setField(PARA_SELECTOR, to_string(selector));
        entry->setField(PARA_FUNC_NAME, functionName);
        entry->setField(PARA_CRITICAL_SIZE, toBigEndianString(criticalSize));

        Condition::Ptr cond = table->newCondition();
        cond->EQ(PARA_SELECTOR, to_string(selector));
        auto entries = table->select(PARA_KEY, cond);
        if (entries->size() == 0)
        {
            table->insert(PARA_KEY, entry, make_shared<AccessOptions>(origin), false);
        }
        else
        {
            table->update(PARA_KEY, entry, cond, make_shared<AccessOptions>(origin));
        }

        out = abi.abiIn("", u256(CODE_SUCCESS));
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("PARA") << LOG_DESC("registerParallelFunction success")
                               << LOG_KV(PARA_SELECTOR, to_string(selector))
                               << LOG_KV(PARA_FUNC_NAME, functionName)
                               << LOG_KV(PARA_CRITICAL_SIZE, criticalSize);
    }
}

void ParallelConfigPrecompiled::unregisterParallelFunction(
    dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef data, Address const& origin,
    bytes& out)
{
    // unregisterParallelFunctionInternal(address,string)
    // unregisterParallelFunctionInternal(address contractAddress, string functionName)
    Address contractAddress;
    string functionName;

    ContractABI abi;
    abi.abiOut(data, contractAddress, functionName);
    uint32_t selector = getFuncSelector(functionName);

    Table::Ptr table = openTable(context, contractAddress, origin);
    if (table && table.get())
    {
        Condition::Ptr cond = table->newCondition();
        cond->EQ(PARA_SELECTOR, to_string(selector));
        table->remove(PARA_KEY, cond, make_shared<AccessOptions>(origin));
    }
    out = abi.abiIn("", u256(CODE_SUCCESS));
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("PARA") << LOG_DESC("unregisterParallelFunction success")
                           << LOG_KV(PARA_SELECTOR, to_string(selector));
}


ParallelConfig::Ptr ParallelConfigPrecompiled::getParallelConfig(
    dev::blockverifier::ExecutiveContext::Ptr context, Address const& contractAddress,
    uint32_t selector, Address const& origin)
{
    Table::Ptr table = openTable(context, contractAddress, origin, false);
    if (!table || !table.get())
    {
        return nullptr;
    }
    Condition::Ptr cond = table->newCondition();
    cond->EQ(PARA_SELECTOR, to_string(selector));
    auto entries = table->select(PARA_KEY, cond);
    if (entries->size() == 0)
    {
        return nullptr;
    }
    else
    {
        auto entry = entries->get(0);
        string funtionName = entry->getField(PARA_FUNC_NAME);
        u256 criticalSize = fromBigEndian<u256, string>(entry->getField(PARA_CRITICAL_SIZE));
        return make_shared<ParallelConfig>(ParallelConfig{funtionName, criticalSize});
    }
}
