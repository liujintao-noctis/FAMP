#include "ArchaeologyMetadataDialog.h"

#include "ArchaeologyMetadata.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace
{
QString valueForKey(const QMap<QString, QString>& fields, const QString& key)
{
    for (auto iterator = fields.cbegin(); iterator != fields.cend(); ++iterator)
    {
        if (iterator.key().compare(key, Qt::CaseInsensitive) == 0)
            return iterator.value();
    }
    return {};
}

bool isStandardField(const QString& key)
{
    return !famp::archaeology::canonicalStandardKey(key).isEmpty();
}

QString itemText(const QTableWidget& table, int row, int column)
{
    const QTableWidgetItem* item = table.item(row, column);
    return item ? item->text() : QString();
}
}

namespace famp::archaeology
{
bool editFields(QWidget* parent,
                const QString& layerName,
                const QString& sourcePath,
                const QMap<QString, QString>& currentFields,
                QMap<QString, QString>& updatedFields)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("考古图层属性"));
    dialog.resize(640, 700);
    QVBoxLayout rootLayout(&dialog);

    QLabel sourceLabel(
        QStringLiteral("<b>%1</b><br><span style='color:#666'>%2</span>")
            .arg(layerName.toHtmlEscaped(), sourcePath.toHtmlEscaped()),
        &dialog);
    sourceLabel.setTextFormat(Qt::RichText);
    sourceLabel.setWordWrap(true);
    rootLayout.addWidget(&sourceLabel);

    QGroupBox standardGroup(QStringLiteral("标准田野记录"), &dialog);
    QFormLayout standardLayout(&standardGroup);
    QMap<QString, QLineEdit*> lineEditors;
    QPlainTextEdit* descriptionEditor = nullptr;
    for (const FieldDefinition& field : standardFields())
    {
        if (field.multiline)
        {
            descriptionEditor = new QPlainTextEdit(
                valueForKey(currentFields, field.key), &standardGroup);
            descriptionEditor->setMinimumHeight(90);
            standardLayout.addRow(field.label, descriptionEditor);
        }
        else
        {
            auto* editor = new QLineEdit(
                valueForKey(currentFields, field.key), &standardGroup);
            editor->setMaxLength(16 * 1024);
            lineEditors.insert(field.key, editor);
            standardLayout.addRow(field.label, editor);
        }
    }
    rootLayout.addWidget(&standardGroup);

    QGroupBox customGroup(QStringLiteral("自定义字段"), &dialog);
    QVBoxLayout customLayout(&customGroup);
    QTableWidget customTable(&customGroup);
    customTable.setColumnCount(2);
    customTable.setHorizontalHeaderLabels(
        {QStringLiteral("字段名称"), QStringLiteral("字段值")});
    customTable.horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    customTable.horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    customTable.setSelectionBehavior(QAbstractItemView::SelectRows);
    customTable.setSelectionMode(QAbstractItemView::ExtendedSelection);
    customTable.setAlternatingRowColors(true);
    for (auto iterator = currentFields.cbegin();
         iterator != currentFields.cend(); ++iterator)
    {
        if (isStandardField(iterator.key()))
            continue;
        const int row = customTable.rowCount();
        customTable.insertRow(row);
        customTable.setItem(row, 0, new QTableWidgetItem(iterator.key()));
        customTable.setItem(row, 1, new QTableWidgetItem(iterator.value()));
    }
    customLayout.addWidget(&customTable);

    QHBoxLayout customButtons;
    QPushButton addButton(QStringLiteral("添加字段"), &customGroup);
    QPushButton removeButton(QStringLiteral("删除所选"), &customGroup);
    customButtons.addWidget(&addButton);
    customButtons.addWidget(&removeButton);
    customButtons.addStretch();
    customLayout.addLayout(&customButtons);
    rootLayout.addWidget(&customGroup, 1);

    QObject::connect(&addButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = customTable.rowCount();
        customTable.insertRow(row);
        customTable.setItem(row, 0, new QTableWidgetItem);
        customTable.setItem(row, 1, new QTableWidgetItem);
        customTable.setCurrentCell(row, 0);
        customTable.editItem(customTable.item(row, 0));
    });
    QObject::connect(&removeButton, &QPushButton::clicked, &dialog, [&]() {
        QSet<int> selectedRows;
        for (const QModelIndex& index : customTable.selectionModel()->selectedRows())
            selectedRows.insert(index.row());
        QList<int> rows = selectedRows.values();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows)
            customTable.removeRow(row);
    });

    QDialogButtonBox buttonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttonBox.button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    rootLayout.addWidget(&buttonBox);
    QObject::connect(&buttonBox, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);

    QMap<QString, QString> candidate;
    QObject::connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, [&]() {
        QMap<QString, QString> collected;
        QSet<QString> keys;
        QString collectionError;
        auto appendField = [&](const QString& inputKey, const QString& inputValue) {
            const QString trimmedKey = inputKey.trimmed();
            const QString trimmedValue = inputValue.trimmed();
            if (trimmedKey.isEmpty() && trimmedValue.isEmpty())
                return true;
            if (trimmedKey.isEmpty())
            {
                collectionError = QStringLiteral("自定义字段名称不能为空。");
                return false;
            }
            if (trimmedValue.isEmpty())
                return true;
            const QString standardKey = canonicalStandardKey(trimmedKey);
            const QString key = standardKey.isEmpty() ? trimmedKey : standardKey;
            const QString normalizedKey = key.toCaseFolded();
            if (keys.contains(normalizedKey))
            {
                collectionError = QStringLiteral("字段名称不能重复：%1").arg(key);
                return false;
            }
            keys.insert(normalizedKey);
            collected.insert(key, trimmedValue);
            return true;
        };

        for (const FieldDefinition& field : standardFields())
        {
            const QString value = field.multiline
                ? (descriptionEditor ? descriptionEditor->toPlainText() : QString())
                : lineEditors.value(field.key)->text();
            if (!appendField(field.key, value))
                break;
        }
        for (int row = 0; collectionError.isEmpty()
             && row < customTable.rowCount(); ++row)
        {
            appendField(itemText(customTable, row, 0),
                        itemText(customTable, row, 1));
        }

        QString validationError;
        if (!collectionError.isEmpty()
            || !normalizeFields(collected, candidate, &validationError))
        {
            QMessageBox::warning(
                &dialog, QStringLiteral("考古图层属性"),
                collectionError.isEmpty() ? validationError : collectionError);
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted)
        return false;
    updatedFields = candidate;
    return true;
}
}
