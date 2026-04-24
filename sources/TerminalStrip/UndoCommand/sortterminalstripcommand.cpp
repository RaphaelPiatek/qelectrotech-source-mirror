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
#include "sortterminalstripcommand.h"
#include "../terminalstrip.h"
#include "../physicalterminal.h"
#include "../realterminal.h"

SortTerminalStripCommand::SortTerminalStripCommand(TerminalStrip *strip, QUndoCommand *parent) :
	QUndoCommand(parent),
	m_strip(strip)
{
	setText(QObject::tr("Trier le bornier %1").arg(m_strip->name()));
	m_old_order = m_strip->physicalTerminal();
	m_new_order = m_strip->physicalTerminal();
	sort();
}

void SortTerminalStripCommand::undo()
{
	if (m_strip) {
		m_strip->setOrderTo(m_old_order);
	}
}

void SortTerminalStripCommand::redo()
{
	if (m_strip) {
		m_strip->setOrderTo(m_new_order);
	}
}

/**
 * @brief terminalIndex
 * Extract a sortable integer from a terminal label.
 * Handles two common formats:
 *   "-XD0:1"  → looks for number after ':'  → 1
 *   "1"       → leading digits               → 1
 * Returns -1 if no number is found.
 */
static int terminalIndex(const QString &label)
{
	// First try: number after colon, e.g. "-XD0:1" or "XD0:10"
	static const QRegularExpression afterColon(QStringLiteral(":(\\d+)"));
	auto m = afterColon.match(label);
	if (m.hasMatch()) {
		return m.captured(1).toInt();
	}

	// Fallback: leading digits, e.g. plain "1", "10"
	static const QRegularExpression leading(QStringLiteral("^(\\d+)"));
	m = leading.match(label);
	if (m.hasMatch()) {
		return m.captured(1).toInt();
	}

	return -1;
}

void SortTerminalStripCommand::sort()
{
	std::sort(m_new_order.begin(), m_new_order.end(), [](QSharedPointer<PhysicalTerminal> arg1, QSharedPointer<PhysicalTerminal> arg2)
	{
		QString str1;
		QString str2;
		int int1 = -1;
		int int2 = -1;

		if (arg1->realTerminalCount())
		{
			str1 = arg1->realTerminals().constLast()->label();
			int1 = terminalIndex(str1);
		}

		if (arg2->realTerminalCount())
		{
			str2 = arg2->realTerminals().constLast()->label();
			int2 = terminalIndex(str2);
		}

			//Sort numerically if both labels have a number and they differ.
			//Otherwise fall back to string comparison.
		if (int1 >= 0 && int2 >= 0 && int1 != int2) {
			return int1 < int2;
		} else {
			return str1 < str2;
		}
	});
}
