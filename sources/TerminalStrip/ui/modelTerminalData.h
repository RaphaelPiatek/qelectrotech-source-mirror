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
#ifndef MODELTERMINALDATA_H
#define MODELTERMINALDATA_H

#include <QString>
#include "../../qetgraphicsitem/element.h"
#include "../../qetinformation.h"
#include "../realterminal.h"

struct modelRealTerminalData
{
		/// Build a modelRealTerminalData for \p real_t.
		/// \p pair_index selects which conductor pair to show as Ziel1/Ziel2:
		///   0 = a/b (default, normal 2-pole terminal)
		///   1 = c/d (second pair, e.g. 4-pole distribution terminal)
		///   2 = e/f (third pair, e.g. 6-pole distribution terminal)
		static modelRealTerminalData data(const QSharedPointer<RealTerminal> &real_t, int pair_index = 0)
		{
			modelRealTerminalData mrtd;
			if (!real_t.isNull())
			{
				mrtd.level_                = real_t->level();
				mrtd.label_                = real_t->label();
				mrtd.Xref_                 = real_t->Xref();
				mrtd.cable_                = real_t->cable();
				mrtd.cable_wire            = real_t->cableWire();
				mrtd.wire_section_         = real_t->wireSection();
				mrtd.ziel1_                = real_t->ziel1ForPair(pair_index);
				mrtd.ziel2_                = real_t->ziel2ForPair(pair_index);
				mrtd.connection_pair_index_= pair_index;
				mrtd.conductor_            = real_t->conductor();
				mrtd.led_                  = real_t->isLed();
				mrtd.type_                 = real_t->type();
				mrtd.function_             = real_t->function();
				mrtd.element_              = real_t->element();
				mrtd.real_terminal         = real_t.toWeakRef();
				mrtd.bridged_              = real_t->isBridged();
				if (auto *elem = real_t->element()) {
					const auto &info = elem->elementData().m_informations;
					mrtd.manufacturer_  = info.value(QETInformation::ELMT_MANUFACTURER).toString();
					mrtd.article_number_= info.value(QETInformation::ELMT_MANUFACTURER_REF).toString();
				}
			}

			return mrtd;
		}

		int level_ = -1;
		int connection_pair_index_ = 0; ///< Which a-b pair this row shows (0=a/b, 1=c/d, 2=e/f)
		QString label_;
		QString Xref_;
		QString cable_;
		QString cable_wire;
		QString wire_section_;  ///< Wire cross-section
		QString ziel1_;         ///< External target for this pair: "BMK:pin"
		QString ziel2_;         ///< Internal target for this pair: "BMK:pin"
		QString conductor_;
		QString manufacturer_;
		QString article_number_;
		bool led_ = false;
		bool bridged_ = false;

		ElementData::TerminalType type_ = ElementData::TerminalType::TTGeneric;
		ElementData::TerminalFunction function_ = ElementData::TerminalFunction::TFGeneric;
		QPointer<Element> element_;

		QWeakPointer<RealTerminal> real_terminal;
};

struct modelPhysicalTerminalData
{
		QVector<modelRealTerminalData> real_data;
		int pos_ = -1;
		QUuid uuid_;
};

inline bool operator == (const modelPhysicalTerminalData &data_1, const modelPhysicalTerminalData &data_2) {
	return data_1.uuid_ == data_2.uuid_;
}

#endif // MODELTERMINALDATA_H
