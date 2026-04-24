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
#ifndef REALTERMINAL_H
#define REALTERMINAL_H

#include <QSharedPointer>
#include <QDomElement>
#include <QList>
#include "../properties/elementdata.h"

class TerminalStrip;
class Element;
class TerminalElement;
class PhysicalTerminal;
class TerminalStripBridge;

/**
 * @brief The RealTerminal class
 * Represent a real terminal.
 * A real terminal can be a drawn terminal in a folio
 * or a terminal set by user but not present
 * on any folio (for example a reserved terminal).
 *
 * When create a new instance of RealTerminal you must
 * call sharedRef() and only use the returned QSharedPointer
 * instead of the raw pointer
 */
class RealTerminal
{
		friend class TerminalElement;
		friend class PhysicalTerminal;

	public:
		/**
		 * @brief The ConnectionData struct
		 * Holds the data for one conductor connection on a terminal element.
		 * Each terminal element has one or more connection points (labeled
		 * "a", "b", "c", etc.). This struct captures the cable/wire data
		 * and the target element info for one such connection point.
		 */
		struct ConnectionData {
			QString terminal_letter; ///< Connection point name on this element: "a", "b", "c", ...
			QString cable;           ///< Cable identifier (m_cable from ConductorProperties)
			QString wire_color;      ///< Wire color identifier (m_wire_color)
			QString wire_section;    ///< Wire cross-section (m_wire_section)
			QString target_bmk;      ///< Label of the target element (BMK / Betriebsmittelkennzeichen)
			QString target_pin;      ///< Terminal name on the target element (connection point)
			QString target_xref;     ///< Folio reference (Xref) of the resolved target element
			QString target_plant;    ///< Plant (=) of the resolved target element
			QString target_location; ///< Location (+) of the resolved target element
		};

	private:
		RealTerminal(Element *element);

		QSharedPointer<RealTerminal> sharedRef();
		QSharedPointer<RealTerminal> sharedRef() const;
		QWeakPointer<RealTerminal> weakRef();

		void setPhysicalTerminal(const QSharedPointer<PhysicalTerminal> &phy_t);

	public:
		~RealTerminal();
		TerminalStrip *parentStrip() const noexcept;
		QSharedPointer<PhysicalTerminal> physicalTerminal() const noexcept;

		QDomElement toXml(QDomDocument &parent_document) const;

		int level() const;
		QString label() const;
		QString Xref() const;
		QString cable() const;
		QString cableWire() const;
		QString wireSection() const;
		/// Returns the pair indices (0=a/b, 1=c/d, 2=e/f) that have at least
		/// one conductor connected.  Always contains at least [0].
		/// Use this instead of connectionPairCount() to avoid off-by-one
		/// errors when an element only uses e.g. pair 1 (c-side only).
		QList<int> activePairIndices() const;
		QString ziel1ForPair(int pair) const;
		QString ziel2ForPair(int pair) const;
		QString conductor() const;

		/// Returns one ConnectionData entry per conductor connection on this element,
		/// sorted by terminal letter ("a" first, then "b", "c", …).
		QList<ConnectionData> connections() const;

		ElementData::TerminalType type() const;
		ElementData::TerminalFunction function() const;

		bool isLed() const;
		bool isElement() const;
		bool isBridged() const;

		QSharedPointer<TerminalStripBridge> bridge() const;

		Element* element() const;
		QUuid elementUuid() const;

		static QString xmlTagName();

	private :
		QPointer<Element> m_element;
		QWeakPointer<RealTerminal> m_this_weak;
		QSharedPointer<PhysicalTerminal> m_physical_terminal;
};

#endif // REALTERMINAL_H
