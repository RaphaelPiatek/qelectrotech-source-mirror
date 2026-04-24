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
#include "freeterminaleditor.h"
#include "ui_freeterminaleditor.h"

#include "../undocommand/changeelementdatacommand.h"
#include "../../diagram.h"
#include "../../elementprovider.h"
#include "freeterminalmodel.h"
#include "../terminalstrip.h"
#include "../terminalstripdata.h"
#include "../UndoCommand/addterminaltostripcommand.h"
#include "../UndoCommand/addterminalstripcommand.h"
#include "../realterminal.h"

#include <QRegularExpression>
#include <QMap>

/**
 * @brief FreeTerminalEditor::FreeTerminalEditor
 * @param project
 * @param parent
 */
FreeTerminalEditor::FreeTerminalEditor(QETProject *project, QWidget *parent) :
	QWidget(parent),
    ui(new Ui::FreeTerminalEditor),
    m_project(project)
{
	ui->setupUi(this);
	ui->m_table_view->setItemDelegate(new FreeTerminalModelDelegate(ui->m_table_view));

    m_model = new FreeTerminalModel(m_project, this);
	ui->m_table_view->setModel(m_model);
	ui->m_table_view->setCurrentIndex(m_model->index(0,0));

    if (m_project) {
        connect(m_project, &QObject::destroyed, this, &FreeTerminalEditor::reload);
    }

		//Disabled the move if the table is currently edited (yellow cell)
	connect(m_model, &FreeTerminalModel::dataChanged, this, [=] {
		this->setDisabledMove();
	});

	connect(ui->m_table_view, &QAbstractItemView::doubleClicked, this, [=](const QModelIndex &index)
	{
		if (m_model->columnTypeForIndex(index) == FreeTerminalModel::XRef)
		{
			auto mrtd = m_model->dataAtRow(index.row());
			if (mrtd.element_)
			{
				auto elmt = mrtd.element_;
				auto diagram = elmt->diagram();
				if (diagram)
				{
					diagram->showMe();
					if (diagram->views().size())
					{
						auto fit_view = elmt->sceneBoundingRect();
						fit_view.adjust(-200,-200,200,200);
						diagram->views().at(0)->fitInView(fit_view, Qt::KeepAspectRatioByExpanding);
					}
				}
			}
		}
	});
}

/**
 * @brief FreeTerminalEditor::~FreeTerminalEditor
 */
FreeTerminalEditor::~FreeTerminalEditor()
{
	delete ui;
}

/**
 * @brief FreeTerminalEditor::reload
 * Reload the editor to be up to date with
 * the current state of the project.
 * Every not applied change will be lost.
 */
void FreeTerminalEditor::reload()
{
	m_model->clear();
	ui->m_move_in_cb->clear();

	if (m_project)
	{
		const auto strip_vector = m_project->terminalStrip();
		for (const auto &strip : strip_vector)
		{
			QString str(strip->installation() + " " + strip->location() + " " + strip->name());
			ui->m_move_in_cb->addItem(str, strip->uuid());
		}
		setDisabledMove(false);
	}
}

/**
 * @brief FreeTerminalEditor::apply
 * Applu current edited values
 */
void FreeTerminalEditor::apply()
{
	const auto modified_data = m_model->modifiedModelRealTerminalData();
	if (modified_data.size())
	{
		m_project->undoStack()->beginMacro(tr("Modifier des propriétés de borniers"));

		for (const auto &data_ : modified_data)
		{
			if (auto element_ = data_.element_)
			{
				auto current_data = element_->elementData();
				current_data.setTerminalType(data_.type_);
				current_data.setTerminalFunction(data_.function_);
				current_data.setTerminalLED(data_.led_);
				current_data.m_informations.addValue(QStringLiteral("label"), data_.label_);

				if (element_->elementData() != current_data) {
					m_project->undoStack()->push(new ChangeElementDataCommand(element_, current_data));
				}
			}
		}

		m_project->undoStack()->endMacro();
	}

	reload();
}

/**
 * @brief FreeTerminalEditor::setProject
 * Set @project as project handled by this editor.
 * If a previous project was setted, everything is clear.
 * This function track the destruction of the project,
 * that  mean if the project pointer is deleted
 * no need to call this function with a nullptr,
 * everything is made inside this class.
 * @param project
 */
void FreeTerminalEditor::setProject(QETProject *project)
{
    if(m_project) {
        disconnect(m_project, &QObject::destroyed, this, &FreeTerminalEditor::reload);
    }
    m_project = project;
    if (m_model) {
        m_model->setProject(project);
    }
    if (m_project) {
        connect(m_project, &QObject::destroyed, this, &FreeTerminalEditor::reload);
    }
    reload();
}

void FreeTerminalEditor::on_m_type_cb_activated(int index)
{
	if (m_model)
	{
		const auto index_list = ui->m_table_view->selectionModel()->selectedIndexes();

		for (auto model_index : index_list)
		{
			auto type_index = m_model->index(model_index.row(), FreeTerminalModel::Type, model_index.parent());
			if (type_index.isValid())
			{
				ElementData::TerminalType override_type;
				switch (index) {
					case 0:
						override_type = ElementData::TTGeneric; break;
					case 1:
						override_type = ElementData::TTFuse; break;
					case 2:
						override_type = ElementData::TTSectional; break;
					case 3:
						override_type = ElementData::TTDiode; break;
					case 4:
						override_type = ElementData::TTGround; break;
					default:
						override_type = ElementData::TTGeneric; break;
				}
				m_model->setData(type_index, override_type);	
			}
		}
	}
}


void FreeTerminalEditor::on_m_function_cb_activated(int index)
{
	if (m_model)
	{
		const auto index_list = ui->m_table_view->selectionModel()->selectedIndexes();

		for (auto model_index : index_list)
		{
			auto function_index = m_model->index(model_index.row(), FreeTerminalModel::Function, model_index.parent());
			if (function_index.isValid())
			{
				ElementData::TerminalFunction override_function;
				switch (index) {
					case 0:
						override_function = ElementData::TFGeneric; break;
					case 1:
						override_function = ElementData::TFPhase; break;
					case 2:
						override_function = ElementData::TFNeutral; break;
					default:
						override_function = ElementData::TFGeneric; break;
				}
				m_model->setData(function_index, override_function);
			}
		}
	}
}


void FreeTerminalEditor::on_m_led_cb_activated(int index)
{
	if (m_model)
	{
		const auto index_list = ui->m_table_view->selectionModel()->selectedIndexes();

		for (auto model_index : index_list)
		{
			auto led_index = m_model->index(model_index.row(), FreeTerminalModel::Led, model_index.parent());

			if (led_index.isValid()) {
				m_model->setData(led_index,
								 index == 0 ? false : true);
			}
		}
	}
}


void FreeTerminalEditor::on_m_move_pb_clicked()
{
		//Get the selected real terminal
	const auto index_list = ui->m_table_view->selectionModel()->selectedIndexes();
	const auto real_t_vector = m_model->realTerminalForIndex(index_list);
	if (real_t_vector.isEmpty()) {
		return;
	}

		//Get the terminal strip who receive the real terminal
	const auto strip_uuid = ui->m_move_in_cb->currentData().toUuid();
	TerminalStrip *terminal_strip{nullptr};
	for (const auto &strip : m_project->terminalStrip()) {
		if (strip->uuid() == strip_uuid) {
			terminal_strip = strip;
			break;
		}
	}

	if (!terminal_strip) {
		return;
	}

	m_project->undoStack()->push(new AddTerminalToStripCommand(real_t_vector, terminal_strip));

	reload();
}

void FreeTerminalEditor::setDisabledMove(bool b)
{
	ui->m_move_label->setDisabled(b);
	ui->m_move_in_cb->setDisabled(b);
	ui->m_move_pb->setDisabled(b);
}

/**
 * @brief FreeTerminalEditor::on_m_auto_assign_pb_clicked
 *
 * Parses each free terminal's label to extract the strip name and terminal
 * index, then assigns all matching terminals to the corresponding
 * TerminalStrip.  Terminals with the same label (e.g. two elements both
 * labelled "-XD0:1") are grouped into one PhysicalTerminal so that
 * multi-pole terminals composed of several element symbols are handled
 * correctly.  Missing strips are created automatically.
 *
 * Label convention (same as Python qet_klemmplan.py):
 *   -XD0:1      → strip "XD0", index "1"
 *   -XD0:1.a-b  → strip "XD0", index "1"  (letter suffix ignored)
 *   XD0:3       → strip "XD0", index "3"   (leading "-" optional)
 */
void FreeTerminalEditor::on_m_auto_assign_pb_clicked()
{
	if (!m_project) return;

	const int rows = m_model->rowCount(QModelIndex());
	if (rows == 0) return;

	// Regex: group 1 = strip name (e.g. "XD0"), group 2 = terminal index (e.g. "1")
	static const QRegularExpression labelRx(
		QStringLiteral("^-?([A-Za-z]+\\d*):([^.]+)(?:\\.[A-Za-z-]+)?$"));

	// Two-level map: strip name → terminal index (numeric) → RealTerminals
	// Terminals sharing the same strip name AND same index belong to one PhysicalTerminal.
	// Using int as the inner key ensures natural numeric ordering (1, 2, 10)
	// instead of lexicographic ordering (1, 10, 2).
	QMap<QString, QMap<int, QVector<QSharedPointer<RealTerminal>>>> byStripAndIndex;

	for (int row = 0; row < rows; ++row) {
		const auto mrtd = m_model->dataAtRow(row);
		const QString label = mrtd.label_.trimmed();
		if (label.isEmpty()) continue;

		auto match = labelRx.match(label);
		if (!match.hasMatch()) continue;

		const QString stripName = match.captured(1); // e.g. "XD0"
		const int termIndex = match.captured(2).toInt(); // e.g. 1, 2, 10 — sorted numerically
		const auto rt = mrtd.real_terminal.toStrongRef();
		if (!rt) continue;

		byStripAndIndex[stripName][termIndex].append(rt);
	}

	if (byStripAndIndex.isEmpty()) return;

	m_project->undoStack()->beginMacro(tr("Auto-assign terminals to strips"));

	for (auto stripIt = byStripAndIndex.constBegin(); stripIt != byStripAndIndex.constEnd(); ++stripIt) {
		const QString                            &stripName = stripIt.key();
		const QMap<int, QVector<QSharedPointer<RealTerminal>>> &byIndex = stripIt.value();

		// Find existing strip with matching name (case-insensitive)
		TerminalStrip *strip = nullptr;
		for (auto *s : m_project->terminalStrip()) {
			if (s->name().compare(stripName, Qt::CaseInsensitive) == 0) {
				strip = s;
				break;
			}
		}

		// Create strip if not found
		if (!strip) {
			strip = new TerminalStrip(QStringLiteral("="),
									  QStringLiteral("+"),
									  stripName,
									  m_project);
			m_project->undoStack()->push(new AddTerminalStripCommand(strip, m_project));
		}

		// Build the 2-D vector expected by addAndGroupTerminals:
		// each inner vector = one PhysicalTerminal (all elements with the same index)
		QVector<QVector<QSharedPointer<RealTerminal>>> grouped;
		for (auto idxIt = byIndex.constBegin(); idxIt != byIndex.constEnd(); ++idxIt) {
			grouped.append(idxIt.value());
		}

		m_project->undoStack()->push(new AddTerminalToStripCommand(grouped, strip));
	}

	m_project->undoStack()->endMacro();

	reload();
}

