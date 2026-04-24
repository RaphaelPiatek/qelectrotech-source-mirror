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
#ifndef KLEMMPLANEXPORTER_H
#define KLEMMPLANEXPORTER_H

#include <QDomDocument>
#include <QSet>
#include <QString>
#include <QVector>

class QETProject;
class TerminalStrip;
class Diagram;

/**
 * @brief The KlemmplanExporter class
 *
 * Generates a terminal-plan (Klemmplan) as an embedded QET element,
 * registers it in the project's own element collection, and places it
 * on the given diagram page — all in one step, undoable via the undo stack.
 *
 * Layout is ported 1:1 from the Python reference script
 * Klemmplan/qet_embed_export.py.
 */
class KlemmplanExporter
{
public:
	/// Build the Klemmplan graphic for @p strip, add its definition to
	/// the project's embedded element collection, and place it on @p diagram.
	static void insertIntoProject(QETProject          *project,
	                               Diagram             *diagram,
	                               const TerminalStrip *strip);

private:
	// ── data ────────────────────────────────────────────────────────────────
	struct Row {
		QString idx;
		QString left_contact,  right_contact;
		QString left_plant,    left_location;
		QString left_bmk,  left_pin,  left_cable,  left_color,  left_section;
		QString right_plant,   right_location;
		QString right_bmk, right_pin, right_cable, right_color, right_section;
		QString function_str;
		QString left_folio, right_folio;
	};

	// ── row building ────────────────────────────────────────────────────────
	static QVector<Row> buildRows(const TerminalStrip *strip);

	// ── XML definition builder ───────────────────────────────────────────────
	static QDomElement buildDefinition(QDomDocument        &doc,
	                                    const TerminalStrip *strip,
	                                    const QVector<Row>  &rows);

	// ── low-level drawing primitives ────────────────────────────────────────
	static void addText  (QDomDocument &doc, QDomElement &desc,
	                      int x, int y, const QString &text,
	                      int size = 9, bool bold = false);
	static void addLine  (QDomDocument &doc, QDomElement &desc,
	                      int x1, int y1, int x2, int y2,
	                      const QString &weight = QStringLiteral("normal"),
	                      bool dashed = false);
	static void addRect  (QDomDocument &doc, QDomElement &desc,
	                      int x, int y, int w, int h);
	static void addCircle(QDomDocument &doc, QDomElement &desc,
	                      int x, int y, int diameter);

	// ── composite drawing routines ──────────────────────────────────────────
	static void drawHeaderFrame  (QDomDocument &doc, QDomElement &desc, int bottomY);
	static void drawTableColumns (QDomDocument &doc, QDomElement &desc, int bottomY);
	static void drawRowGrid      (QDomDocument &doc, QDomElement &desc,
	                               const QVector<Row> &rows);
	static void drawCableInventory(QDomDocument &doc, QDomElement &desc,
	                                const QVector<Row> &rows,
	                                QMap<QString,int>  &cable_to_id_out);
	static void drawDataRows     (QDomDocument &doc, QDomElement &desc,
	                               const QVector<Row>      &rows,
	                               const QMap<QString,int> &cable_to_id);
	static void drawBridges      (QDomDocument &doc, QDomElement &desc,
	                               const TerminalStrip     *strip,
	                               const QVector<Row>      &rows,
	                               const QMap<QString,int> &row_y_by_idx_level);

	// ── misc helpers ────────────────────────────────────────────────────────
	static QString safeName       (const QString &text);
	static QString elementFileName(const TerminalStrip *strip);
	static QString displayName    (const TerminalStrip *strip);
	static QString uid            ();
	static QString parseWireCount (const QString &name,
	                                const QSet<QString> &colors);
};

#endif // KLEMMPLANEXPORTER_H
