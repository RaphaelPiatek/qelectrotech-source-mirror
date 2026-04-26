/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef TERMINALSTRIPMODEL_H
#define TERMINALSTRIPMODEL_H

#include <QAbstractTableModel>
#include <QObject>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QHash>
#include <QColor>

#include "../terminalstrip.h"
#include "modelTerminalData.h"

//Code to use QColor as key for QHash
inline uint qHash(const QColor &key, uint seed) {
	return qHash(key.name(), seed);
}

//needed to use QPointer<Element> as key of QHash
inline uint qHash(const QPointer<Element> &key, uint seed) {
	if (key)
		return qHash(key->uuid(), seed);
	else
		return qHash(QUuid(), seed);
}

class TerminalStrip;

class TerminalStripModel : public QAbstractTableModel
{
	public:
		enum Column {
			Pos = 0,
			Level = 1,
			Level0 = 2,
			Level1 = 3,
			Level2 = 4,
			Level3 = 5,
			Label = 6,
			Conductor = 7,
			XRef = 8,
			Cable = 9,
			CableWire = 10,
			WireSection = 11,
			CableRight = 12,
			CableWireRight = 13,
			WireSectionRight = 14,
			Ziel1 = 15,
			Ziel2 = 16,
			Type = 17,
			Function = 18,
			Led = 19,
			Manufacturer = 20,
			Article = 21,
			Invalid = 99
		};

		static int levelForColumn(TerminalStripModel::Column column);
		static TerminalStripModel::Column columnTypeForIndex(const QModelIndex &index);

		Q_OBJECT
	public:
		TerminalStripModel(TerminalStrip *terminal_strip, QObject *parent = nullptr);
		void setTerminalStrip(TerminalStrip *terminal_strip);

		virtual int rowCount    (const QModelIndex &parent = QModelIndex()) const override;
		virtual int columnCount (const QModelIndex &parent = QModelIndex()) const override;
		virtual QVariant data   (const QModelIndex &index, int role = Qt::DisplayRole) const override;
		virtual bool setData (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
		virtual QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
		virtual Qt::ItemFlags flags (const QModelIndex &index) const override;
		QVector<modelRealTerminalData> modifiedmodelRealTerminalData() const;

		QVector<modelPhysicalTerminalData> modelPhysicalTerminalDataForIndex(QModelIndexList index_list) const;
		QVector<modelRealTerminalData> modelRealTerminalDataForIndex(QModelIndexList index_list) const;
		modelRealTerminalData modelRealTerminalDataForIndex(const QModelIndex &index) const;

		void buildBridgePixmap(const QSize &pixmap_size);

		/// Returns the number of PhysicalTerminals in the model.
		int physicalCount() const { return m_physical_data.size(); }
		/// Returns the number of model rows contributed by physical terminal at
		/// \p physical_index (includes one row per active conductor pair per
		/// real terminal, so it can exceed realTerminalCount()).
		int rowCountForPhysicalIndex(int physical_index) const {
			if (physical_index < 0 || physical_index >= m_physical_data.size()) return 0;
			return m_physical_data.at(physical_index).real_data.size();
		}

		void reload();

	private:
		void fillPhysicalTerminalData();
		void precomputeBridgePixmaps();
		modelRealTerminalData dataAtRow(int row) const;
		void replaceDataAtRow(modelRealTerminalData data, int row);
		modelPhysicalTerminalData physicalDataAtIndex(int index) const;
		modelRealTerminalData realDataAtIndex(int index) const;
		QPixmap bridgePixmapFor(const QModelIndex &index) const;

	private:
		QPointer<TerminalStrip> m_terminal_strip;
		QHash<QPointer<Element>, QVector<bool>> m_modified_cell;
		QVector<modelPhysicalTerminalData> m_physical_data;
		struct BridgePixmap
		{
				QPixmap top_,
						middle_,
						bottom_,
						none_;
		};

		QHash<QColor, BridgePixmap> m_bridges_pixmaps;

};

class TerminalStripModelDelegate : public QStyledItemDelegate
{
	Q_OBJECT

	public:
		TerminalStripModelDelegate(QObject *parent = Q_NULLPTR);

		QWidget *createEditor(
				QWidget *parent,
				const QStyleOptionViewItem &option,
				const QModelIndex &index) const override;
		void setModelData(
				QWidget *editor,
				QAbstractItemModel *model,
				const QModelIndex &index) const override;

		void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

#endif // TERMINALSTRIPMODEL_H
