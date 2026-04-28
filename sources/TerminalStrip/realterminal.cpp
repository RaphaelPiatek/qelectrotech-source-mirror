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
#include "realterminal.h"
#include "terminalstrip.h"
#include <QSet>
#include <utility>
#include <QDebug>
#include "../qetgraphicsitem/terminalelement.h"
#include "physicalterminal.h"
#include "../qetgraphicsitem/conductor.h"
#include "../qetgraphicsitem/terminal.h"
#include "../qetgraphicsitem/element.h"
#include "../diagram.h"
#include "../qetproject.h"
#include "../qetinformation.h"

/**
 * @brief RealTerminal
 * @param parent_strip : parent terminal strip
 * @param terminal : terminal element (if any) in a folio
 */
RealTerminal::RealTerminal(Element *terminal) :
	m_element(terminal)
{}

RealTerminal::~RealTerminal()
{
	if (m_physical_terminal) {
		m_physical_terminal->removeTerminal(sharedRef());
	}
}

/**
 * @brief RealTerminal::sharedRef
 * @return a QSharedPointer of this
 */
QSharedPointer<RealTerminal> RealTerminal::sharedRef()
{
	QSharedPointer<RealTerminal> this_shared(this->weakRef());
	if (this_shared.isNull())
	{
		this_shared = QSharedPointer<RealTerminal>(this);
		m_this_weak = this_shared.toWeakRef();
	}

	return this_shared;
}

/**
 * @brief RealTerminal::sharedRef
 * @return a shared reference of this, not that because
 * this method is const, the shared reference can be null if not already
 * used in another part of the code.
 */
QSharedPointer<RealTerminal> RealTerminal::sharedRef() const {
	return QSharedPointer<RealTerminal>(m_this_weak);
}

/**
 * @brief RealTerminal::weakRef
 * @return a QWeakPointer of this, weak pointer can be bull
 */
QWeakPointer<RealTerminal> RealTerminal::weakRef() {
	return m_this_weak;
}

/**
 * @brief toXml
 * @param parent_document
 * @return this real terminal to xml
 */
QDomElement RealTerminal::toXml(QDomDocument &parent_document) const
{
	auto root_elmt = parent_document.createElement(this->xmlTagName());
	if (m_element)
		root_elmt.setAttribute(QStringLiteral("element_uuid"), m_element->uuid().toString());

	return root_elmt;
}

/**
 * @brief RealTerminal::setPhysicalTerminal
 * Set the parent physical terminal of this real terminal
 * @param phy_t
 */
void RealTerminal::setPhysicalTerminal(const QSharedPointer<PhysicalTerminal> &phy_t) {
	m_physical_terminal = phy_t;
}

/**
* @brief parentStrip
* @return parent terminal strip or nullptr
*/
TerminalStrip *RealTerminal::parentStrip() const noexcept {
	if (m_physical_terminal) {
		return m_physical_terminal->terminalStrip();
	} else {
		return nullptr;
	}
}

/**
 * @brief RealTerminal::physicalTerminal
 * @return The parent physical terminal of this terminal.
 * The returned QSharedPointer can be null
 */
QSharedPointer<PhysicalTerminal> RealTerminal::physicalTerminal() const noexcept{
	return m_physical_terminal;
}

/**
 * @brief RealTerminal::level
 * @return
 */
int RealTerminal::level() const
{
	if (m_physical_terminal &&
		sharedRef()) {
		return m_physical_terminal->levelOf(sharedRef());
	}

	return -1;
}

/**
 * @brief label
 * @return the label of this real terminal
 */
QString RealTerminal::label() const {
	if (!m_element.isNull()) {
		return m_element->actualLabel();
	} else {
		return QLatin1String();
	}
}

/**
 * @brief RealTerminal::Xref
 * @return Convenient method to get the XRef
 * formatted to string
 */
QString RealTerminal::Xref() const
{
	if (!m_element.isNull()) {
		return autonum::AssignVariables::genericXref(m_element.data());
	} else {
		return QString();
	}
}

// Local result struct carrying BMK, pin, and the resolved element pointer
struct ResolveResult { QString bmk; QString pin; Element *elem = nullptr; };

// Forward declaration
static ResolveResult resolveTarget(
		Element *elem, const QString &pin,
		Element *skipSelf, QSet<Element*> &visited,
		const QString &conductorFunction, QETProject *project);

/**
 * @brief followFromReport
 * Given a folio-report element \p report on the OTHER folio, follow all
 * outgoing conductors and return the first real target found.
 */
static ResolveResult followFromReport(
		Element *report, Element *skipSelf,
		QSet<Element*> &visited, const QString &conductorFunction,
		QETProject *project)
{
	if (visited.contains(report)) return {};
	visited.insert(report);

	for (Terminal *rt : report->terminals()) {
		for (Conductor *rc : rt->conductors()) {
			Terminal *farTerminal = (rc->terminal1 == rt) ? rc->terminal2 : rc->terminal1;
			if (!farTerminal || !farTerminal->parentElement()) continue;
			auto res = resolveTarget(farTerminal->parentElement(), farTerminal->name(),
									 skipSelf, visited, conductorFunction, project);
			if (!res.bmk.isEmpty() || !res.pin.isEmpty()) return res;
		}
	}
	return {};
}

/**
 * @brief resolveTarget
 * Given the immediate neighbour \p elem / \p pin of a conductor, resolve
 * the real target element by following folio-report links (Seitenverweise)
 * and Terminale pass-through elements as many hops as necessary.
 *
 * Strategy for folio reports (mirrors Python qet_klemmplan.py):
 *  1. Primary: use elem->linkedElements() if QET has linked them in memory.
 *  2. Fallback: search all project diagrams for an inverse-type report element
 *     whose conductors share the same function attribute as the incoming
 *     conductor — exactly as Python does when linked_refs is empty.
 *
 * @param elem               Immediate neighbour element
 * @param pin                Terminal name on \p elem
 * @param skipSelf           Our own element — never returned as target
 * @param visited            Loop guard
 * @param conductorFunction  m_function of the conductor that led here
 * @param project            QETProject for project-wide fallback search
 */
static ResolveResult resolveTarget(
		Element *elem, const QString &pin,
		Element *skipSelf, QSet<Element*> &visited,
		const QString &conductorFunction, QETProject *project)
{
	if (!elem || visited.contains(elem) || elem == skipSelf)
		return {};
	visited.insert(elem);

	const int lt = elem->linkType();

	// ── Folio-report element (Seitenverweis) ─────────────────────────────────
	if (lt & Element::AllReport)
	{
		// ── Strategy 1: explicit QET link (linkedElements) ────────────────────
		const auto linked = elem->linkedElements();
		for (Element *otherReport : linked) {
			auto res = followFromReport(otherReport, skipSelf, visited,
										conductorFunction, project);
			if (!res.bmk.isEmpty() || !res.pin.isEmpty()) return res;
		}

		// ── Strategy 2: project-wide search for inverse report type ──────────
		if (project) {
			const Element::kind inverseType =
					(lt == Element::NextReport) ? Element::PreviousReport
												: Element::NextReport;

			for (Diagram *d : project->diagrams()) {
				for (Element *candidate : d->elements()) {
					if (candidate->linkType() != inverseType) continue;
					if (visited.contains(candidate)) continue;

					if (!conductorFunction.isEmpty()) {
						bool funcMatch = false;
						for (Terminal *t : candidate->terminals()) {
							for (Conductor *c : t->conductors()) {
								if (c->properties().m_function == conductorFunction) {
									funcMatch = true;
									break;
								}
							}
							if (funcMatch) break;
						}
						if (!funcMatch) continue;
					}

					auto res = followFromReport(candidate, skipSelf, visited,
												conductorFunction, project);
					if (!res.bmk.isEmpty() || !res.pin.isEmpty()) return res;
				}
			}
		}
		return {};
	}

	// ── Terminale terminal strip element ─────────────────────────────────────
	if (lt & Element::Terminale)
	{
		if (project) {
			for (TerminalStrip *strip : project->terminalStrip()) {
				for (const auto &phy : strip->physicalTerminal()) {
					for (const auto &rt : phy->realTerminals()) {
						if (rt->element() == elem) {
							const QString bmk = strip->name().startsWith(QLatin1Char('-'))
							    ? strip->name()
							    : QStringLiteral("-") + strip->name();
							return {bmk, pin, elem};
						}
					}
				}
			}
		}
		return {elem->actualLabel(), pin, elem};
	}

	// ── Real target element ───────────────────────────────────────────────────
	return {elem->actualLabel(), pin, elem};
}

/**
 * @brief RealTerminal::connections
 * Iterates over all terminal connection points (a, b, c, …) of this
 * terminal element and returns a ConnectionData entry for each conductor
 * found.
 *
 * Cross-folio references (Seitenverweise) and Terminale pass-through
 * elements are followed transparently using resolveTarget().
 *
 * The list is sorted alphabetically by terminal_letter ("a" first).
 * @return sorted list of ConnectionData, one entry per connected conductor
 */
QList<RealTerminal::ConnectionData> RealTerminal::connections() const
{
	QList<ConnectionData> result;
	if (!m_element) return result;

	for (Terminal *terminal : m_element->terminals()) {
		for (Conductor *conductor : terminal->conductors()) {
			ConnectionData cd;
			cd.terminal_letter = terminal->name().toLower();

			const ConductorProperties &props = conductor->properties();
			cd.cable        = props.m_cable;
			cd.wire_color   = props.m_wire_color;
			cd.wire_section = props.m_wire_section;

			// Immediate neighbour on the other end of this conductor
			Terminal *other = (conductor->terminal1 == terminal)
					? conductor->terminal2
					: conductor->terminal1;

			if (other && other->parentElement()) {
				QSet<Element*> visited { m_element.data() };
				QETProject *project = m_element->diagram()
						? m_element->diagram()->project()
						: nullptr;
				auto res = resolveTarget(
						other->parentElement(),
						other->name(),
						m_element.data(),
						visited,
						conductor->properties().m_function,
						project);
				cd.target_bmk  = res.bmk;
				cd.target_pin  = res.pin;
				cd.target_xref = res.elem
						? autonum::AssignVariables::genericXref(res.elem)
						: QString();
				if (res.elem) {
					const auto &info = res.elem->elementData().m_informations;
					cd.target_plant    = info.value(QETInformation::ELMT_PLANT).toString();
					cd.target_location = info.value(QETInformation::ELMT_LOCATION).toString();
				}
			}

			result.append(cd);
		}
	}

	// Sort by terminal letter so "a" < "b" < "c" …
	std::sort(result.begin(), result.end(),
		[](const ConnectionData &a, const ConnectionData &b) {
			return a.terminal_letter < b.terminal_letter;
		});

	return result;
}

/**
 * @brief RealTerminal::cable
 * Returns the cable identifier of the first conductor attached to this
 * terminal element (letter "a" preferred, otherwise the first found).
 */
QString RealTerminal::cable() const {
	const auto conns = connections();
	for (const auto &cd : conns)
		if (!cd.cable.isEmpty()) return cd.cable;
	return QString();
}

/**
 * @brief RealTerminal::cableWire
 * Returns the wire color of the first conductor attached to this terminal
 * element (letter "a" preferred, otherwise the first found).
 */
QString RealTerminal::cableWire() const {
	const auto conns = connections();
	for (const auto &cd : conns)
		if (!cd.wire_color.isEmpty()) return cd.wire_color;
	return QString();
}

/**
 * @brief RealTerminal::wireSection
 * Returns the wire cross-section of the first conductor attached to this
 * terminal element.
 */
QString RealTerminal::wireSection() const {
	const auto conns = connections();
	for (const auto &cd : conns)
		if (!cd.wire_section.isEmpty()) return cd.wire_section;
	return QString();
}

namespace {
	// The three standard conductor-pair letter groups: pair 0 = a/b, pair 1 = c/d, pair 2 = e/f
	static const QString LEFT_LETTERS[]  = { QLatin1String("a"), QLatin1String("c"), QLatin1String("e") };
	static const QString RIGHT_LETTERS[] = { QLatin1String("b"), QLatin1String("d"), QLatin1String("f") };
	static constexpr int MAX_PAIRS = 3;

	QString zielString(const RealTerminal::ConnectionData &cd) {
		QString z = cd.target_bmk;
		if (!cd.target_pin.isEmpty()) z += QLatin1Char(':') + cd.target_pin;
		return z;
	}
}

/**
 * @brief RealTerminal::activePairIndices
 * Returns the list of pair indices (0=a/b, 1=c/d, 2=e/f) for which at
 * least one conductor is connected on this element.
 * Always returns at least [0] so every terminal gets one display row.
 *
 * This is the right method to use for model population: an element that
 * only uses terminal "c" will return [1], not [0], which ensures that
 * ziel1ForPair(1) (looking for "c") is called rather than ziel1ForPair(0)
 * (looking for "a", which would always be empty).
 */
QList<int> RealTerminal::activePairIndices() const
{
	const auto conns = connections();
	QSet<QString> letters;
	for (const auto &cd : conns) letters.insert(cd.terminal_letter);

	QList<int> indices;
	for (int i = 0; i < MAX_PAIRS; ++i) {
		if (letters.contains(LEFT_LETTERS[i]) || letters.contains(RIGHT_LETTERS[i]))
			indices.append(i);
	}
	if (indices.isEmpty()) indices.append(0); // always at least one row
	return indices;
}

/**
 * @brief RealTerminal::ziel1ForPair
 * Returns the external-side target ("BMK:pin") for connection pair \p pair
 * (pair 0 = letter "a", pair 1 = "c", pair 2 = "e").
 */
QString RealTerminal::ziel1ForPair(int pair) const
{
	if (pair < 0 || pair >= MAX_PAIRS) return QString();
	const QString &letter = LEFT_LETTERS[pair];
	for (const auto &cd : connections()) {
		if (cd.terminal_letter == letter) return zielString(cd);
	}
	return QString();
}

/**
 * @brief RealTerminal::ziel2ForPair
 * Returns the internal-side target ("BMK:pin") for connection pair \p pair
 * (pair 0 = letter "b", pair 1 = "d", pair 2 = "f").
 */
QString RealTerminal::ziel2ForPair(int pair) const
{
	if (pair < 0 || pair >= MAX_PAIRS) return QString();
	const QString &letter = RIGHT_LETTERS[pair];
	for (const auto &cd : connections()) {
		if (cd.terminal_letter == letter) return zielString(cd);
	}
	return QString();
}

/**
 * @brief RealTerminal::conductor
 * @return
 */
QString RealTerminal::conductor() const {
	if (m_element)
	{
		const auto conductors_{m_element->conductors()};
		if (conductors_.size()) {
			return conductors_.first()->properties().text;
		}
	}
	return QString();
}

/**
 * @brief RealTerminal::type
 * @return
 */
ElementData::TerminalType RealTerminal::type() const {
	if (m_element) {
		return m_element->elementData().terminalType();
	} else {
		return ElementData::TTGeneric;
	}
}

/**
 * @brief RealTerminal::function
 * @return
 */
ElementData::TerminalFunction RealTerminal::function() const {
	if (m_element) {
		return m_element->elementData().terminalFunction();
	} else {
		return ElementData::TFGeneric;
	}
}

/**
 * @brief RealTerminal::isLed
 * @return
 */
bool RealTerminal::isLed() const {
	if (m_element) {
		return m_element->elementData().terminalLed();
	} else {
		return false;
	}
}

/**
 * @brief isElement
 * @return true if this real terminal is linked to a terminal element
 */
bool RealTerminal::isElement() const {
	return m_element.isNull() ? false : true;
}

/**
 * @brief RealTerminal::isBridged
 * @return true if is bridged.
 * @sa TerminalStrip::isBridged
 */
bool RealTerminal::isBridged() const
{
	if (parentStrip()) {
		return !parentStrip()->isBridged(m_this_weak.toStrongRef()).isNull();
	} else {
		return false;
	}
}

/**
 * @brief RealTerminal::bridge
 * @return
 */
QSharedPointer<TerminalStripBridge> RealTerminal::bridge() const
{
	if (parentStrip()) {
		return parentStrip()->isBridged(m_this_weak.toStrongRef());
	} else {
		return QSharedPointer<TerminalStripBridge>();
	}
}

/**
 * @brief element
 * @return the element linked to this real terminal
 * or nullptr if not linked to an Element.
 */
Element *RealTerminal::element() const {
	return m_element.data();
}

/**
 * @brief elementUuid
 * @return if this real terminal is an element
 * in a folio, return the uuid of the element
 * else return a null uuid.
 */
QUuid RealTerminal::elementUuid() const {
	if (!m_element.isNull()) {
		return m_element->uuid();
	} else {
		return QUuid();
	}
}

/**
 * @brief RealTerminal::RealTerminal::xmlTagName
 * @return
 */
QString RealTerminal::RealTerminal::xmlTagName() {
	return QStringLiteral("real_terminal");
}
