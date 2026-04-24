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
#include "klemmplanexporter.h"

#include "terminalstrip.h"
#include "physicalterminal.h"
#include "realterminal.h"
#include "terminalstripbridge.h"
#include "../qetproject.h"
#include "../diagram.h"
#include "../ElementsCollection/xmlelementcollection.h"
#include "../ElementsCollection/elementslocation.h"
#include "../NameList/nameslist.h"
#include "../factory/elementfactory.h"
#include "../undocommand/addgraphicsobjectcommand.h"
#include "../qetgraphicsitem/element.h"
#include "../qetgraphicsitem/terminal.h"
#include "../qetgraphicsitem/conductor.h"

#include <QMessageBox>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

// ────────────────────────────────────────────────────────────────────────────
// Layout constants
// Design space: 1555 wide × dynamic height (20 px per row)
// Target width : 1410  (height stays unscaled — computed from row count)
// ────────────────────────────────────────────────────────────────────────────
static constexpr double K_SX = 1499.0 / 1555.0;
static inline int kx(int v) { return qRound(v * K_SX); }
static inline int ky(int v) { return v; }

// ────────────────────────────────────────────────────────────────────────────
// File-local helpers
// ────────────────────────────────────────────────────────────────────────────

static QString extractIdx(const QString &label)
{
	const int colon = label.indexOf(QLatin1Char(':'));
	if (colon >= 0) {
		QString after = label.mid(colon + 1);
		const int dot = after.indexOf(QLatin1Char('.'));
		if (dot >= 0) after = after.left(dot);
		return after.trimmed();
	}
	QString v = label.trimmed();
	if (v.startsWith(QLatin1Char('-'))) v = v.mid(1);
	return v.isEmpty() ? QStringLiteral("?") : v;
}

static int levelFromLeftContact(const QString &left)
{
	if (left == QLatin1String("a") || left == QLatin1String("b")) return 1;
	if (left == QLatin1String("c") || left == QLatin1String("d")) return 2;
	if (left == QLatin1String("e") || left == QLatin1String("f")) return 3;
	return 1;
}

// Determine pair level (1=a/b, 2=c/d, 3=e/f) for a bridge RT by examining
// the element's terminal contact names — this works even when no conductors
// are connected (activePairIndices() falls back to [0] in that case, which
// would give the wrong level for "1-Pol-c-d" elements at physical level 0).
static int pairLevelForBridgeRT(const QSharedPointer<RealTerminal> &rt)
{
	Element *elem = rt->element();
	if (!elem) return 1;

	bool hasAB = false, hasCD = false, hasEF = false;
	for (Terminal *t : elem->terminals()) {
		if (t->name().isEmpty()) continue;
		const QChar l = t->name().at(0).toLower();
		if (l == QLatin1Char('a') || l == QLatin1Char('b')) hasAB = true;
		else if (l == QLatin1Char('c') || l == QLatin1Char('d')) hasCD = true;
		else if (l == QLatin1Char('e') || l == QLatin1Char('f')) hasEF = true;
	}

	// Single-pair element: determined by which contacts exist on the element
	if (!hasAB && hasEF) return 3;
	if (!hasAB && hasCD) return 2;
	if (!hasAB)          return 1;

	// Multi-pair element (double-deck a/b+c/d): use physical level to distinguish
	const int physLevel = rt->level();
	if (physLevel == 2) return 3;
	if (physLevel == 1) return 2;
	return 1;
}

// ============================================================================
// Public entry point
// ============================================================================

/**
 * @brief KlemmplanExporter::insertIntoProject
 *
 * 1. Builds the Klemmplan element definition for @p strip.
 * 2. Registers it in the project's embedded element collection
 *    under "import/Klemmenplaene/".
 * 3. Creates the Element object and places it on @p diagram
 *    via an undo-able AddGraphicsObjectCommand.
 */
void KlemmplanExporter::insertIntoProject(QETProject          *project,
                                           Diagram             *diagram,
                                           const TerminalStrip *strip)
{
	if (!project || !diagram || !strip) return;

	XmlElementCollection *collection = project->embeddedElementCollection();

	// ── 1. Ensure "import/Klemmenplaene" directory ─────────────────────────
	{
		NamesList names;
		names.addName(QStringLiteral("de"), QStringLiteral("Klemmenpläne"));
		names.addName(QStringLiteral("en"), QStringLiteral("terminal diagrams"));
		names.addName(QStringLiteral("fr"), QStringLiteral("Schémas de terminaux"));
		collection->createDir(QStringLiteral("import"),
		                      QStringLiteral("Klemmenplaene"),
		                      names);
	}

	// ── 2. Build the <definition> element ──────────────────────────────────
	const QVector<Row> rows = buildRows(strip);
	QDomDocument defDoc;
	QDomElement  definition = buildDefinition(defDoc, strip, rows);

	// ── 3. Remove old element with same name (if any) ──────────────────────
	const QString fname   = elementFileName(strip);
	const QString dirPath = QStringLiteral("import/Klemmenplaene");
	const QString elPath  = dirPath % QLatin1Char('/') % fname;

	if (collection->exist(elPath))
		collection->removeElement(elPath);

	// ── 4. Add the new definition ──────────────────────────────────────────
	if (!collection->addElementDefinition(dirPath, fname, definition)) {
		QMessageBox::critical(nullptr,
			QObject::tr("Klemmplan"),
			QObject::tr("Element konnte nicht in die Sammlung eingefügt werden."));
		return;
	}

	// ── 5. Instantiate the element ─────────────────────────────────────────
	// Collection path: embed://import/Klemmenplaene/<fname>
	const QString embedPath = QStringLiteral("embed://") % elPath;
	ElementsLocation loc(embedPath, project);

	if (!loc.exist()) {
		QMessageBox::critical(nullptr,
			QObject::tr("Klemmplan"),
			QObject::tr("ElementsLocation nicht gefunden: ") + embedPath);
		return;
	}

	int state = 0;
	Element *element = ElementFactory::Instance()->createElement(loc, nullptr, &state);
	if (state || !element) {
		delete element;
		QMessageBox::critical(nullptr,
			QObject::tr("Klemmplan"),
			QObject::tr("Element konnte nicht erstellt werden."));
		return;
	}

	// ── 6. Place on the diagram ────────────────────────────────────────────
	diagram->undoStack().push(
		new AddGraphicsObjectCommand(element, diagram, QPointF(30, 30)));
}

// ============================================================================
// Row building
// ============================================================================

QVector<KlemmplanExporter::Row> KlemmplanExporter::buildRows(const TerminalStrip *strip)
{
	static const QLatin1String LEFT[]  = { QLatin1String("a"), QLatin1String("c"), QLatin1String("e") };
	static const QLatin1String RIGHT[] = { QLatin1String("b"), QLatin1String("d"), QLatin1String("f") };

	QVector<Row> rows;

	// Sort physical terminals numerically by position index so the Klemmplan
	// always shows terminals in numeric order regardless of GUI ordering.
	auto physList = strip->physicalTerminal();
	std::sort(physList.begin(), physList.end(),
		[](const QSharedPointer<PhysicalTerminal> &a, const QSharedPointer<PhysicalTerminal> &b) {
			const auto ra = a->realTerminals();
			const auto rb = b->realTerminals();
			const QString ia = ra.isEmpty() ? QString() : extractIdx(ra.first()->label());
			const QString ib = rb.isEmpty() ? QString() : extractIdx(rb.first()->label());
			bool okA, okB;
			const int na = ia.toInt(&okA), nb = ib.toInt(&okB);
			if (okA && okB) return na < nb;
			return ia < ib; // fallback: lexicographic
		});

	for (const auto &phy : physList) {
		auto realList = phy->realTerminals();
		if (realList.isEmpty()) continue;

		// Sort by pair index (from activePairIndices) so rows always appear a/b → c/d → e/f.
		// Physical level ≠ pair index: e.g. a "1-Pol-c-d" element can sit at level 0.
		std::sort(realList.begin(), realList.end(),
			[](const QSharedPointer<RealTerminal> &a, const QSharedPointer<RealTerminal> &b) {
				const auto ai = a->activePairIndices();
				const auto bi = b->activePairIndices();
				return (ai.isEmpty() ? 0 : ai.first()) < (bi.isEmpty() ? 0 : bi.first());
			});

		const QString idx = extractIdx(realList.first()->label());

		for (const auto &rt : realList) {
			const auto conns = rt->connections();

			// Pair index comes from activePairIndices() — this correctly reflects
			// which terminal letters ("a"/"b", "c"/"d", "e"/"f") the element uses,
			// regardless of its physical level in the terminal block.
			QList<int> pairIndices = rt->activePairIndices();
			std::sort(pairIndices.begin(), pairIndices.end());

			// ── Collect function per pair index (0=a, 1=c, 2=e) ─────────────
			QMap<int, QString> functionPerPair;
			if (Element *elem = rt->element()) {
				QMap<int, QSet<QString>> fnsPerPair;
				for (Terminal *t : elem->terminals()) {
					if (t->name().isEmpty()) continue;
					const QChar l = t->name().at(0).toLower();
					int pi = -1;
					if      (l == 'a') pi = 0;
					else if (l == 'c') pi = 1;
					else if (l == 'e') pi = 2;
					else continue;
					for (Conductor *c : t->conductors()) {
						const QString fn = c->properties().m_function.trimmed();
						if (!fn.isEmpty()) fnsPerPair[pi].insert(fn);
					}
				}
				for (auto it = fnsPerPair.begin(); it != fnsPerPair.end(); ++it) {
					QStringList sorted = QStringList(it.value().begin(), it.value().end());
					sorted.sort();
					functionPerPair[it.key()] = sorted.join(QStringLiteral(" / "));
				}
			}

			for (const int pairIdx : pairIndices) {
				Row row;
				row.idx           = idx;
				row.left_contact  = QString(LEFT[pairIdx]);
				row.right_contact = QString(RIGHT[pairIdx]);

				// ── Connection data lookup ────────────────────────────────────
				for (const auto &cd : conns) {
					if (cd.terminal_letter == row.left_contact) {
						row.left_plant    = cd.target_plant;
						row.left_location = cd.target_location;
						row.left_bmk     = cd.target_bmk;
						row.left_pin     = cd.target_pin;
						row.left_cable   = cd.cable;
						row.left_color   = cd.wire_color;
						row.left_section = cd.wire_section;
						row.left_folio   = cd.target_xref;
					} else if (cd.terminal_letter == row.right_contact) {
						row.right_plant    = cd.target_plant;
						row.right_location = cd.target_location;
						row.right_bmk     = cd.target_bmk;
						row.right_pin     = cd.target_pin;
						row.right_cable   = cd.cable;
						row.right_color   = cd.wire_color;
						row.right_section = cd.wire_section;
						row.right_folio   = cd.target_xref;
					}
				}

				row.function_str = functionPerPair.value(pairIdx);

				rows.append(row);
			}
		}
	}

	return rows;
}

// ============================================================================
// XML definition builder
// ============================================================================

QDomElement KlemmplanExporter::buildDefinition(QDomDocument        &doc,
                                                const TerminalStrip *strip,
                                                const QVector<Row>  &rows)
{
	// Dynamic height: header up to y=245, then 20 px per data row, plus border
	const int dataBottom = 245 + (int)rows.size() * 20;
	const int elemHeight = dataBottom + 10; // 10 px bottom border

	QDomElement definition = doc.createElement(QStringLiteral("definition"));
	definition.setAttribute(QStringLiteral("link_type"),  QStringLiteral("simple"));
	definition.setAttribute(QStringLiteral("hotspot_y"),  QStringLiteral("10"));
	definition.setAttribute(QStringLiteral("height"),     QString::number(elemHeight));
	definition.setAttribute(QStringLiteral("version"),    QStringLiteral("%{version}"));
	definition.setAttribute(QStringLiteral("hotspot_x"),  QStringLiteral("2"));
	definition.setAttribute(QStringLiteral("width"),      QStringLiteral("1499"));
	definition.setAttribute(QStringLiteral("type"),       QStringLiteral("element"));

	QDomElement uuidEl = doc.createElement(QStringLiteral("uuid"));
	uuidEl.setAttribute(QStringLiteral("uuid"), uid());
	definition.appendChild(uuidEl);

	QDomElement names = doc.createElement(QStringLiteral("names"));
	QDomElement nameDe = doc.createElement(QStringLiteral("name"));
	nameDe.setAttribute(QStringLiteral("lang"), QStringLiteral("de"));
	nameDe.appendChild(doc.createTextNode(
		QStringLiteral("Terminal strip %1 / 1").arg(displayName(strip))));
	names.appendChild(nameDe);
	definition.appendChild(names);

	definition.appendChild(doc.createElement(QStringLiteral("elementInformations")));
	definition.appendChild(doc.createElement(QStringLiteral("informations")));

	QDomElement desc = doc.createElement(QStringLiteral("description"));
	definition.appendChild(desc);

	// Static geometry
	drawHeaderFrame(doc, desc, dataBottom);
	drawTableColumns(doc, desc, dataBottom);
	drawRowGrid(doc, desc, rows);

	// Cable inventory + header texts
	QMap<QString, int> cableToId;
	drawCableInventory(doc, desc, rows, cableToId);

	// Strip name (large label in title block)
	addText(doc, desc, 770, 40, displayName(strip), 28, true);

	// Build row_y_by_idx_level map for bridge drawing
	QMap<QString, int> rowYByIdxLevel;
	{
		int y = ky(260);
		for (const auto &row : rows) {
			const int level = levelFromLeftContact(row.left_contact);
			const QString key = row.idx + QLatin1Char('|') + QString::number(level);
			rowYByIdxLevel[key] = y;
			y += ky(20);
		}
	}

	drawDataRows(doc, desc, rows, cableToId);
	drawBridges(doc, desc, strip, rows, rowYByIdxLevel);

	return definition;
}

// ============================================================================
// Low-level drawing primitives
// ============================================================================

void KlemmplanExporter::addText(QDomDocument &doc, QDomElement &desc,
                                 int x, int y, const QString &text,
                                 int size, bool bold)
{
	if (text.isEmpty()) return;
	QDomElement t = doc.createElement(QStringLiteral("text"));
	t.setAttribute(QStringLiteral("font"),
		QStringLiteral("Liberation Sans,%1,-1,5,%2,0,0,0,0,0").arg(size).arg(bold ? 75 : 50));
	t.setAttribute(QStringLiteral("rotation"), QStringLiteral("0"));
	t.setAttribute(QStringLiteral("y"),     ky(y));
	t.setAttribute(QStringLiteral("color"), QStringLiteral("#000000"));
	t.setAttribute(QStringLiteral("x"),     kx(x));
	t.setAttribute(QStringLiteral("text"),  text);
	desc.appendChild(t);
}

void KlemmplanExporter::addLine(QDomDocument &doc, QDomElement &desc,
                                 int x1, int y1, int x2, int y2,
                                 const QString &weight, bool dashed)
{
	const QString style =
		QStringLiteral("line-style:%1;line-weight:%2;filling:none;color:black")
		.arg(dashed ? QStringLiteral("dashed") : QStringLiteral("normal"))
		.arg(weight);
	QDomElement l = doc.createElement(QStringLiteral("line"));
	l.setAttribute(QStringLiteral("y1"),        ky(y1));
	l.setAttribute(QStringLiteral("style"),     style);
	l.setAttribute(QStringLiteral("Length1"),   QStringLiteral("1.5"));
	l.setAttribute(QStringLiteral("x1"),        kx(x1));
	l.setAttribute(QStringLiteral("end1"),      QStringLiteral("none"));
	l.setAttribute(QStringLiteral("antialias"), QStringLiteral("false"));
	l.setAttribute(QStringLiteral("x2"),        kx(x2));
	l.setAttribute(QStringLiteral("length2"),   QStringLiteral("1.5"));
	l.setAttribute(QStringLiteral("y2"),        ky(y2));
	l.setAttribute(QStringLiteral("end2"),      QStringLiteral("none"));
	desc.appendChild(l);
}

void KlemmplanExporter::addRect(QDomDocument &doc, QDomElement &desc,
                                 int x, int y, int w, int h)
{
	QDomElement r = doc.createElement(QStringLiteral("rect"));
	r.setAttribute(QStringLiteral("y"),         ky(y));
	r.setAttribute(QStringLiteral("width"),     kx(w));
	r.setAttribute(QStringLiteral("antialias"), QStringLiteral("false"));
	r.setAttribute(QStringLiteral("ry"),        QStringLiteral("0"));
	r.setAttribute(QStringLiteral("x"),         kx(x));
	r.setAttribute(QStringLiteral("rx"),        QStringLiteral("0"));
	r.setAttribute(QStringLiteral("style"),
		QStringLiteral("line-style:normal;line-weight:normal;filling:none;color:black"));
	r.setAttribute(QStringLiteral("height"),    ky(h));
	desc.appendChild(r);
}

void KlemmplanExporter::addCircle(QDomDocument &doc, QDomElement &desc,
                                   int x, int y, int diameter)
{
	QDomElement c = doc.createElement(QStringLiteral("circle"));
	c.setAttribute(QStringLiteral("x"),         kx(x));
	c.setAttribute(QStringLiteral("y"),         ky(y));
	c.setAttribute(QStringLiteral("diameter"),  qMax(1, diameter));
	c.setAttribute(QStringLiteral("antialias"), QStringLiteral("true"));
	c.setAttribute(QStringLiteral("style"),
		QStringLiteral("line-style:normal;line-weight:normal;filling:black;color:black"));
	desc.appendChild(c);
}

// ============================================================================
// Composite drawing routines
// ============================================================================

void KlemmplanExporter::drawHeaderFrame(QDomDocument &doc, QDomElement &desc, int bottomY)
{
	// Outer border spans the full element height (dynamic)
	addRect(doc, desc, 0, -5, 1555, bottomY + 5);

	// Fixed header boxes
	struct Rect { int y, w, x, h; };
	static const Rect rects[] = {
		{ -5,  600,   0, 180 },
		{ -5,  600, 955, 180 },
		{ -5,  355, 600,  60 },
	};
	for (const auto &r : rects)
		addRect(doc, desc, r.x, r.y, r.w, r.h);

	struct Line { int x1, y1, x2, y2; const char *w; };
	static const Line lines[] = {
		{   0,  15,  600,  15, "thin"   },
		{   0,  35,  600,  35, "thin"   },
		{   0,  55,  600,  55, "thin"   },
		{   0,  75,  600,  75, "thin"   },
		{   0,  95,  600,  95, "thin"   },
		{   0, 115,  600, 115, "thin"   },
		{   0, 135,  600, 135, "thin"   },
		{   0, 155,  600, 155, "thin"   },
		{   0, 242, 1555, 242, "normal" },
		{   0, 245, 1555, 245, "normal" },
		{ 955,  15, 1555,  15, "normal" },
	};
	for (const auto &l : lines)
		addLine(doc, desc, l.x1, l.y1, l.x2, l.y2, QLatin1String(l.w));
}

void KlemmplanExporter::drawTableColumns(QDomDocument &doc, QDomElement &desc, int bottomY)
{
	struct Col { int x; const char *w; };
	static const Col cols[] = {
		{   80, "normal" }, {  290, "normal" }, {  580, "normal" },
		{  600, "thin"   }, {  690, "thin"   }, {  810, "hight"  },
		{  870, "hight"  }, {  960, "thin"   }, {  980, "normal" },
		{ 1270, "normal" }, { 1480, "normal" },
	};
	for (const auto &c : cols)
		addLine(doc, desc, c.x, 215, c.x, bottomY, QLatin1String(c.w));

	static const int cableX[] = {
		110, 140, 170, 200, 230, 260, 1300, 1330, 1360, 1390, 1420, 1450
	};
	for (int x : cableX)
		addLine(doc, desc, x, 235, x, bottomY, QStringLiteral("thin"));
}

void KlemmplanExporter::drawRowGrid(QDomDocument &doc, QDomElement &desc,
                                     const QVector<Row> &rows)
{
	const int n = rows.size();
	for (int i = 0; i < n; ++i) {
		const int  y = 245 + i * 20;
		// Draw a thick solid line when the next row belongs to a different terminal
		const bool terminalBoundary = (i == n - 1) || (rows[i].idx != rows[i + 1].idx);
		addLine(doc, desc, 0, y + 20, 1555, y + 20,
			terminalBoundary ? QStringLiteral("normal") : QStringLiteral("thin"),
			/*dashed=*/!terminalBoundary);
		addLine(doc, desc, 630, y + 6, 630, y + 14, QStringLiteral("normal"));
		addLine(doc, desc, 660, y + 6, 660, y + 14, QStringLiteral("normal"));
		addLine(doc, desc, 900, y + 6, 900, y + 14, QStringLiteral("normal"));
		addLine(doc, desc, 930, y + 6, 930, y + 14, QStringLiteral("normal"));
	}
}

void KlemmplanExporter::drawCableInventory(QDomDocument       &doc,
                                            QDomElement        &desc,
                                            const QVector<Row> &rows,
                                            QMap<QString,int>  &cable_to_id_out)
{
	struct CableInfo { QString section; QSet<QString> colors; };
	QVector<QString>         cableOrder;
	QMap<QString, CableInfo> cableMap;

	auto process = [&](const QString &cname, const QString &section, const QString &color) {
		if (cname.isEmpty()) return;
		if (!cableMap.contains(cname)) {
			cableOrder.append(cname);
			cableMap[cname].section = section;
		}
		if (!color.isEmpty()) cableMap[cname].colors.insert(color);
	};
	for (const auto &row : rows) {
		process(row.left_cable,  row.left_section,  row.left_color);
		process(row.right_cable, row.right_section, row.right_color);
	}

	cable_to_id_out.clear();
	for (int i = 0; i < cableOrder.size(); ++i)
		cable_to_id_out[cableOrder.at(i)] = i + 1;

	const int maxCables = qMin(cableOrder.size(), 7);
	for (int i = 0; i < maxCables; ++i) {
		const QString    &cname = cableOrder.at(i);
		const CableInfo  &info  = cableMap[cname];
		const int         yInv  = 30 + (i + 1) * 20 - 10;
		addText(doc, desc,   5, yInv + 10, QString::number(i + 1), 9, true);
		addText(doc, desc,  20, yInv + 10, cname);
		addText(doc, desc, 220, yInv + 10, info.section);
		addText(doc, desc, 350, yInv + 10, parseWireCount(cname, info.colors));
	}

	for (int i = 1; i <= 7; ++i) {
		addText(doc, desc,  80 + (i-1)*30 + 10, 235, QString::number(i), 7, true);
		addText(doc, desc, 1270 + (i-1)*30 + 10, 235, QString::number(i), 7, true);
	}

	addText(doc, desc,  10,  10, QStringLiteral("Cables or single wires used"), 9, true);
	addText(doc, desc,  20,  30, QStringLiteral("Type"));
	addText(doc, desc, 220,  30, QStringLiteral("Cross-section"));
	addText(doc, desc, 350,  30, QStringLiteral("Number of wires"));
	addText(doc, desc, 615,  25, QStringLiteral("Terminal strip:"), 9, true);
	addText(doc, desc, 615,  80, QStringLiteral("Blatt 1 von 1"), 9, true);
	addText(doc, desc, 970,  10, QStringLiteral("Terminal used:"), 9, true);
	addText(doc, desc,1300,  10, QStringLiteral("Comments:"), 9, true);
	addText(doc, desc, 510, 215, QStringLiteral("externaly"));
	addText(doc, desc,1012, 215, QStringLiteral("internaly"));
	addText(doc, desc, 610, 235, QStringLiteral("Bridge"));
	addText(doc, desc, 880, 235, QStringLiteral("Bridge"));
	addText(doc, desc, 700, 235, QStringLiteral("Function"));
	addText(doc, desc,1097, 215, QStringLiteral("Ziehl"), 9, true);
	addText(doc, desc, 415, 215, QStringLiteral("Ziehl"), 9, true);
	addText(doc, desc, 295, 235, QStringLiteral("= Plant / + Location / - Device tag / : Connection"));
	addText(doc, desc, 985, 235, QStringLiteral("= Plant / + Location / - Device tag / : Connection"));
	addText(doc, desc,1310, 215, QStringLiteral("Cable"), 9, true);
	addText(doc, desc, 120, 215, QStringLiteral("Cable"), 9, true);
	addText(doc, desc,  10, 220, QStringLiteral("Plan-"));
	addText(doc, desc,  10, 235, QStringLiteral("reference"));
	addText(doc, desc,1490, 220, QStringLiteral("Plan-"));
	addText(doc, desc,1490, 235, QStringLiteral("reference"));
	addText(doc, desc, 810, 200, QStringLiteral("Terminal"), 11, true);
}

void KlemmplanExporter::drawDataRows(QDomDocument            &doc,
                                      QDomElement             &desc,
                                      const QVector<Row>      &rows,
                                      const QMap<QString,int> &cableToId)
{
	int y = ky(260);
	const int ROW_STEP    = ky(20);
	const int TEXT_OFFSET = ky(-10);
	QString currentIdx;

	for (const auto &row : rows) {
		const int textY = y + 10 + TEXT_OFFSET;

		if (row.idx != currentIdx) {
			currentIdx = row.idx;
			addText(doc, desc, 820, textY, currentIdx, 11, true);
		}

		addText(doc, desc, 585, textY, row.left_contact,  9, true);
		addText(doc, desc, 965, textY, row.right_contact, 9, true);

		if (!row.left_plant.isEmpty())
			addText(doc, desc, 295, textY, QStringLiteral("=") + row.left_plant);
		if (!row.left_location.isEmpty())
			addText(doc, desc, 345, textY, QStringLiteral("+") + row.left_location);
		if (!row.left_bmk.isEmpty())
			addText(doc, desc, 440, textY, row.left_bmk);
		if (!row.left_pin.isEmpty())
			addText(doc, desc, 530, textY, QStringLiteral(":") + row.left_pin);

		if (!row.left_cable.isEmpty()) {
			const int lid = cableToId.value(row.left_cable, 0);
			if (lid >= 1 && lid <= 7)
				addText(doc, desc, 80 + (lid-1)*30 + 5, textY, row.left_color, 7);
		}

		if (!row.right_plant.isEmpty())
			addText(doc, desc, 985, textY, QStringLiteral("=") + row.right_plant);
		if (!row.right_location.isEmpty())
			addText(doc, desc, 1035, textY, QStringLiteral("+") + row.right_location);
		if (!row.right_bmk.isEmpty())
			addText(doc, desc, 1130, textY, row.right_bmk);
		if (!row.right_pin.isEmpty())
			addText(doc, desc, 1220, textY, QStringLiteral(":") + row.right_pin);

		if (!row.right_cable.isEmpty()) {
			const int rid = cableToId.value(row.right_cable, 0);
			if (rid >= 1 && rid <= 7)
				addText(doc, desc, 1270 + (rid-1)*30 + 5, textY, row.right_color, 7);
		}

		if (!row.function_str.isEmpty())
			addText(doc, desc, 700, textY, row.function_str);
		if (!row.left_folio.isEmpty())
			addText(doc, desc, 8, textY, row.left_folio);
		if (!row.right_folio.isEmpty())
			addText(doc, desc, 1490, textY, row.right_folio);

		y += ROW_STEP;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: draw one bridge group (sorted pts, same level) as a vertical line
// with filled circles at each endpoint.
// ─────────────────────────────────────────────────────────────────────────────
static void drawBridgeGroup(QDomDocument &doc, QDomElement &desc,
                             int x,
                             QList<QPair<QString,int>> &pts)
{
	if (pts.size() < 2) return;

	const int YOff = ky(-5); // centre of scaled row
	const int R    = qMax(1, ky(2));

	std::sort(pts.begin(), pts.end(),
		[](const QPair<QString,int> &a, const QPair<QString,int> &b){
			return a.second < b.second;
		});

	for (int i = 0; i + 1 < pts.size(); ++i) {
		QDomElement line = doc.createElement(QStringLiteral("line"));
		line.setAttribute(QStringLiteral("x1"), x);
		line.setAttribute(QStringLiteral("y1"), pts[i].second   + YOff);
		line.setAttribute(QStringLiteral("x2"), x);
		line.setAttribute(QStringLiteral("y2"), pts[i+1].second + YOff);
		line.setAttribute(QStringLiteral("style"),
			QStringLiteral("line-style:normal;line-weight:hight;filling:black;color:black"));
		line.setAttribute(QStringLiteral("antialias"), QStringLiteral("false"));
		desc.appendChild(line);
	}
	for (const auto &pt : pts) {
		QDomElement c = doc.createElement(QStringLiteral("circle"));
		c.setAttribute(QStringLiteral("x"), x - R);
		c.setAttribute(QStringLiteral("y"), pt.second + YOff - R);
		c.setAttribute(QStringLiteral("diameter"),  2 * R);
		c.setAttribute(QStringLiteral("antialias"), QStringLiteral("true"));
		c.setAttribute(QStringLiteral("style"),
			QStringLiteral("line-style:normal;line-weight:normal;filling:black;color:black"));
		desc.appendChild(c);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Collect conductor-based bridge groups by following connectivity.
//
// Strategy: union-find over Terminal* objects (unnamed bridge contacts that
// connect strip elements).  Each connected component = one bridge rail.
// Level is determined by proximity of the bridge contact to named contacts
// (a/b→1, c/d→2, e/f→3) on the same element — so we don't depend on
// terminal ordering or y-sort direction.
//
// Returns: level → list of components, each a list of (idx, y).
// ─────────────────────────────────────────────────────────────────────────────
static QMap<int, QList<QList<QPair<QString,int>>>>
collectConductorBridgeGroups(const TerminalStrip     *strip,
                              const QMap<QString,int> &rowYByIdxLevel)
{
	// ── Element* → position idx ──────────────────────────────────────────────
	QMap<Element*, QString> elemToIdx;
	for (const auto &rt : strip->realTerminals())
		if (rt->element())
			elemToIdx[rt->element()] = extractIdx(rt->label());

	// ── Union-find over Terminal* ─────────────────────────────────────────────
	// Key: Terminal* cast to quintptr so we can use QMap<quintptr, quintptr>
	QMap<quintptr, quintptr> ufParent;
	std::function<quintptr(quintptr)> ufFind = [&](quintptr x) -> quintptr {
		if (!ufParent.contains(x)) ufParent[x] = x;
		if (ufParent[x] != x) ufParent[x] = ufFind(ufParent[x]);
		return ufParent[x];
	};

	// ── Follow bridge conductors and union connected terminals ────────────────
	QSet<Conductor*> visited;
	for (const auto &rt : strip->realTerminals()) {
		Element *elem = rt->element();
		if (!elem) continue;
		for (Terminal *t : elem->terminals()) {
			if (!t->name().isEmpty()) continue; // only unnamed bridge contacts
			for (Conductor *c : t->conductors()) {
				if (visited.contains(c)) continue;
				visited.insert(c);
				Terminal *other = (c->terminal1 == t) ? c->terminal2 : c->terminal1;
				if (!other || !other->name().isEmpty()) continue;
				Element *otherElem = other->parentElement();
				if (!otherElem || !elemToIdx.contains(otherElem)) continue;
				const quintptr ka = reinterpret_cast<quintptr>(t);
				const quintptr kb = reinterpret_cast<quintptr>(other);
				const quintptr ra = ufFind(ka), rb = ufFind(kb);
				if (ra != rb) ufParent[ra] = rb;
			}
		}
	}

	// ── Group terminals by root → one component per bridge rail ──────────────
	QMap<quintptr, QList<Terminal*>> components;
	for (auto it = ufParent.begin(); it != ufParent.end(); ++it)
		components[ufFind(it.key())].append(reinterpret_cast<Terminal*>(it.key()));

	// ── Determine level of each component by proximity to named contacts ──────
	// For each bridge terminal, find the named contact on the same element
	// with the closest element-local y-position.  That contact's level
	// (a/b→1, c/d→2, e/f→3) is the bridge rail's level.
	QMap<int, QList<QList<QPair<QString,int>>>> result;

	for (auto cIt = components.begin(); cIt != components.end(); ++cIt) {
		const QList<Terminal*> &comp = cIt.value();
		if (comp.size() < 2) continue;

		// Level from any one sample terminal
		int level = 1;
		Terminal *sample = comp.first();
		Element  *sElem  = sample->parentElement();
		if (sElem) {
			qreal minDist = std::numeric_limits<qreal>::max();
			for (Terminal *named : sElem->terminals()) {
				if (named->name().isEmpty()) continue;
				const int lv = levelFromLeftContact(named->name().toLower());
				const qreal d = std::abs(named->pos().y() - sample->pos().y());
				if (d < minDist) { minDist = d; level = lv; }
			}
		}

		// Collect (idx, y) for each element in the component
		QSet<QString> seen;
		QList<QPair<QString,int>> pts;
		for (Terminal *t : comp) {
			Element *elem = t->parentElement();
			if (!elem || !elemToIdx.contains(elem)) continue;
			const QString idx = elemToIdx[elem];
			if (seen.contains(idx)) continue;
			seen.insert(idx);
			const QString key = idx + QLatin1Char('|') + QString::number(level);
			if (!rowYByIdxLevel.contains(key)) continue;
			pts.append({idx, rowYByIdxLevel.value(key)});
		}
		if (pts.size() >= 2)
			result[level].append(pts);
	}

	return result;
}

void KlemmplanExporter::drawBridges(QDomDocument            &doc,
                                     QDomElement             &desc,
                                     const TerminalStrip     *strip,
                                     const QVector<Row>      &/*rows*/,
                                     const QMap<QString,int> &rowYByIdxLevel)
{
	const QMap<int,int> xForLevel = { {1, kx(615)}, {2, kx(645)}, {3, kx(675)} };

	// ── Explicit TerminalStripBridge objects ──────────────────────────────────
	QSet<QSharedPointer<TerminalStripBridge>> bridges;
	for (const auto &rt : strip->realTerminals()) {
		const auto b = rt->bridge();
		if (b) bridges.insert(b);
	}

	for (const auto &bridge : bridges) {
		QMap<int, QList<QPair<QString,int>>> byLevel;
		for (const auto &rt : bridge->realTerminals()) {
			const int pairLevel = pairLevelForBridgeRT(rt);
			const QString idx = extractIdx(rt->label());
			const QString key = idx + QLatin1Char('|') + QString::number(pairLevel);
			if (!rowYByIdxLevel.contains(key)) continue;
			byLevel[pairLevel].append({ idx, rowYByIdxLevel.value(key) });
		}
		for (auto it = byLevel.begin(); it != byLevel.end(); ++it)
			drawBridgeGroup(doc, desc, xForLevel.value(it.key(), 615), it.value());
	}

	// ── Conductor-based bridges (automatic detection) ─────────────────────────
	const auto conductorGroups = collectConductorBridgeGroups(strip, rowYByIdxLevel);
	for (auto it = conductorGroups.begin(); it != conductorGroups.end(); ++it) {
		const int x = xForLevel.value(it.key(), 615);
		for (auto group : it.value())
			drawBridgeGroup(doc, desc, x, group);
	}
}

// ============================================================================
// Misc helpers
// ============================================================================

QString KlemmplanExporter::safeName(const QString &text)
{
	static const QRegularExpression re(QStringLiteral("[^A-Za-z0-9_\\-]+"));
	QString v = text.trimmed();
	v.replace(re, QStringLiteral("_"));
	return v.isEmpty() ? QStringLiteral("X") : v;
}

QString KlemmplanExporter::elementFileName(const TerminalStrip *strip) {
	return QStringLiteral("terminal_strip_") + safeName(strip->name()) + QStringLiteral("1.elmt");
}

QString KlemmplanExporter::displayName(const TerminalStrip *strip)
{
	const QString n = strip->name();
	return n.startsWith(QLatin1Char('-')) ? n : QStringLiteral("-") + n;
}

QString KlemmplanExporter::uid() {
	return QUuid::createUuid().toString();
}

QString KlemmplanExporter::parseWireCount(const QString &name, const QSet<QString> &colors)
{
	static const QRegularExpression re(QStringLiteral("(\\d+)\\s*[xXgG]\\s*\\d+"));
	const auto m = re.match(name);
	if (m.hasMatch()) return m.captured(1);
	if (!colors.isEmpty()) return QString::number(colors.size());
	return QStringLiteral("1");
}
