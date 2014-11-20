/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * ConfigSettings.cc
 * Copyright (C) 2013-2014 Sandro Mani <manisandro@gmail.com>
 *
 * gImageReader is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gImageReader is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ConfigSettings.hh"

TableSetting::TableSetting(const QString& key, QTableWidget* table)
	: AbstractSetting(key), m_table(table)
{
	m_table->setRowCount(0);
	QStringList rows = QSettings().value(m_key).toString().split(";", QString::SkipEmptyParts);
	for(int row = 0, nRows = rows.size(); row < nRows; ++row)
	{
		m_table->insertRow(row);
		QStringList cols = rows[row].split(",");
		for(int col = 0, nCols = cols.size(); col < nCols; ++col)
		{
			m_table->setItem(row, col, new QTableWidgetItem(cols[col]));
		}
	}
	connect(m_table, SIGNAL(itemChanged(QTableWidgetItem*)), this, SLOT(serialize()));
}

void TableSetting::serialize()
{
	QStringList rows;
	for(int row = 0, nRows = m_table->rowCount(); row < nRows; ++row)
	{
		QStringList cols;
		for(int col = 0, nCols = m_table->columnCount(); col < nCols; ++col)
		{
			QTableWidgetItem* item = m_table->item(row, col);
			cols.append(item ? item->text() : QString());
		}
		rows.append(cols.join(","));
	}
	QSettings().setValue(m_key, QVariant::fromValue(rows.join(";")));
	emit changed();
}
