/* This file is part of VoltDB.
 * Copyright (C) 2008-2015 VoltDB Inc.
 *
 * This file contains original code and/or modifications of original code.
 * Any modifications made by VoltDB Inc. are licensed under the following
 * terms and conditions:
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with VoltDB.  If not, see <http://www.gnu.org/licenses/>.
 */
/* Copyright (C) 2008 by H-Store Project
 * Brown University
 * Massachusetts Institute of Technology
 * Yale University
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "expressions/rankpercentageexpression.h"

#include "common/debuglog.h"
#include "common/executorcontext.hpp"
#include "common/ValueFactory.hpp"
#include "indexes/tableindex.h"
#include "storage/TableCatalogDelegate.hpp"

namespace voltdb {
    RankPercentageExpression::RankPercentageExpression(
            std::string &percentage,
            std::string &tableName, std::string &indexName,
            int partitionbySize)
        : AbstractExpression(EXPRESSION_TYPE_WINDOWING_RANK_PERCENTAGE),
        m_target_table_name(tableName), m_target_index_name(indexName),
        m_partitionbySize(partitionbySize)
    {
        // make sure it's valid double string in Java
        m_percentage = std::atof(percentage.c_str());

        VoltDBEngine* engine = ExecutorContext::getEngine();
        m_tcd = engine->getTableDelegate(m_target_table_name);
        Table* targetTable = m_tcd->getTable();
        m_tableIndex = targetTable->index(m_target_index_name);

        // allocate more than needed, may not be a problem
        int tupleLen = m_tableIndex->getKeySchema()->tupleLength();
        m_parititonbySearchKeyBackingStore = new char[tupleLen];
        m_parititonbyMaxSearchKeyBackingStore = new char[tupleLen];
    };

    RankPercentageExpression::~RankPercentageExpression() {
        delete []m_parititonbySearchKeyBackingStore;
        delete []m_parititonbyMaxSearchKeyBackingStore;
    }

    voltdb::NValue RankPercentageExpression::eval(const TableTuple *tuple1, const TableTuple *tuple2) const
    {
        if (m_partitionbySize <= 0) {
            // apply to whole table
            int64_t tupleCount = m_tcd->getTable()->activeTupleCount();

            int64_t rank = static_cast<int64_t>((double)tupleCount * m_percentage);
            return ValueFactory::getBigIntValue(rank);
        }

        //
        // calculate group tuple counts
        //
        int64_t rkStart, rkEnd, counts = 0;

        TableTuple partitionbySearchKey = TableTuple(m_tableIndex->getKeySchema());
        partitionbySearchKey.moveNoHeader(m_parititonbySearchKeyBackingStore);
        partitionbySearchKey.setAllNulls();
        IndexCursor partitionbyCursor(m_tableIndex->getTupleSchema());
        m_tableIndex->getIndexedTableTuple(tuple1, partitionbySearchKey, m_partitionbySize);
        rkStart = m_tableIndex->getCounterGET(&partitionbySearchKey, false, partitionbyCursor);

        // need to get the next group rank quickly
        // in order to compute reverse rank within group
        // using max padding search key
        TableTuple partitionbyMaxSearchKey = TableTuple(m_tableIndex->getKeySchema());
        partitionbyMaxSearchKey.moveNoHeader(m_parititonbyMaxSearchKeyBackingStore);
        partitionbyMaxSearchKey.setAllNulls();
        IndexCursor partitionbyMaxCursor(m_tableIndex->getTupleSchema());

        m_tableIndex->getIndexedTableTuple(tuple1, partitionbyMaxSearchKey,
                m_tableIndex->getKeySchema()->columnCount(), m_partitionbySize);
        rkEnd = m_tableIndex->getCounterLET(&partitionbyMaxSearchKey, true, partitionbyMaxCursor);

        std::cout << "rkEnd: " << rkEnd << ", rkStart: " << rkStart << std::endl;
        counts = rkEnd - rkStart + 1;

        int64_t rank = static_cast<int64_t>((double)counts * m_percentage);

        return ValueFactory::getBigIntValue(rank);
    }

}

