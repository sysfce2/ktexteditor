/*
    SPDX-FileCopyrightText: 2009 Rafael Fernández López <ereslibre@kde.org>
    SPDX-FileCopyrightText: 2013 Dominik Haumann <dhaumann@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "katecategorydrawer.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

KateCategoryDrawer::KateCategoryDrawer()
    : KCategoryDrawer(nullptr)
{
}

void KateCategoryDrawer::drawCategory(const QModelIndex &index, int sortRole, const QStyleOption &option, QPainter *painter) const
{
    Q_UNUSED(sortRole)

    painter->setRenderHint(QPainter::Antialiasing);

    const QRect optRect = option.rect;
    QFont font(QApplication::font());
    font.setBold(true);
    const int height = categoryHeight(index, option);
    const bool leftToRight = painter->layoutDirection() == Qt::LeftToRight;

    {
        QRect newOptRect(optRect);

        if (leftToRight) {
            newOptRect.translate(1, 1);
        } else {
            newOptRect.translate(-1, 1);
        }

        // BEGIN: inner top left corner
        {
            painter->save();
            painter->setPen(option.palette.base().color());
            QRectF arc;
            if (leftToRight) {
                const QPointF topLeft(newOptRect.topLeft());
                arc = QRectF(topLeft, QSizeF(4, 4));
                arc.translate(0.5, 0.5);
                painter->drawArc(arc, 1440, 1440);
            } else {
                QPointF topRight(newOptRect.topRight());
                topRight.rx() -= 4;
                arc = QRectF(topRight, QSizeF(4, 4));
                arc.translate(-0.5, 0.5);
                painter->drawArc(arc, 0, 1440);
            }
            painter->restore();
        }
        // END: inner top left corner

        // BEGIN: inner left vertical line
        {
            QPoint start;
            QPoint verticalGradBottom;
            if (leftToRight) {
                start = newOptRect.topLeft();
                verticalGradBottom = newOptRect.topLeft();
            } else {
                start = newOptRect.topRight();
                verticalGradBottom = newOptRect.topRight();
            }
            start.ry() += 3;
            verticalGradBottom.ry() += newOptRect.height() - 3;
            QLinearGradient gradient(start, verticalGradBottom);
            gradient.setColorAt(0, option.palette.base().color());
            gradient.setColorAt(1, Qt::transparent);
            painter->fillRect(QRect(start, QSize(1, newOptRect.height() - 3)), gradient);
        }
        // END: inner left vertical line

        // BEGIN: inner horizontal line
        {
            QPoint start;
            QPoint horizontalGradTop;
            if (leftToRight) {
                start = newOptRect.topLeft();
                horizontalGradTop = newOptRect.topLeft();
                start.rx() += 3;
                horizontalGradTop.rx() += newOptRect.width() - 3;
            } else {
                start = newOptRect.topRight();
                horizontalGradTop = newOptRect.topRight();
                start.rx() -= 3;
                horizontalGradTop.rx() -= newOptRect.width() - 3;
            }
            QLinearGradient gradient(start, horizontalGradTop);
            gradient.setColorAt(0, option.palette.base().color());
            gradient.setColorAt(1, Qt::transparent);
            QSize rectSize;
            if (leftToRight) {
                rectSize = QSize(newOptRect.width() - 3, 1);
            } else {
                rectSize = QSize(-newOptRect.width() + 3, 1);
            }
            painter->fillRect(QRect(start, rectSize), gradient);
        }
        // END: inner horizontal line
    }

    QColor outlineColor = option.palette.text().color();
    outlineColor.setAlphaF(0.35);

    // BEGIN: top left corner
    {
        painter->save();
        painter->setPen(outlineColor);
        QRectF arc;
        if (leftToRight) {
            const QPointF topLeft(optRect.topLeft());
            arc = QRectF(topLeft, QSizeF(4, 4));
            arc.translate(0.5, 0.5);
            painter->drawArc(arc, 1440, 1440);
        } else {
            QPointF topRight(optRect.topRight());
            topRight.rx() -= 4;
            arc = QRectF(topRight, QSizeF(4, 4));
            arc.translate(-0.5, 0.5);
            painter->drawArc(arc, 0, 1440);
        }
        painter->restore();
    }
    // END: top left corner

    // BEGIN: left vertical line
    {
        QPoint start;
        QPoint verticalGradBottom;
        if (leftToRight) {
            start = optRect.topLeft();
            verticalGradBottom = optRect.topLeft();
        } else {
            start = optRect.topRight();
            verticalGradBottom = optRect.topRight();
        }
        start.ry() += 3;
        verticalGradBottom.ry() += optRect.height() - 3;
        QLinearGradient gradient(start, verticalGradBottom);
        gradient.setColorAt(0, outlineColor);
        gradient.setColorAt(1, option.palette.base().color());
        painter->fillRect(QRect(start, QSize(1, optRect.height() - 3)), gradient);
    }
    // END: left vertical line

    // BEGIN: horizontal line
    {
        QPoint start;
        QPoint horizontalGradTop;
        if (leftToRight) {
            start = optRect.topLeft();
            horizontalGradTop = optRect.topLeft();
            start.rx() += 3;
            horizontalGradTop.rx() += optRect.width() - 3;
        } else {
            start = optRect.topRight();
            horizontalGradTop = optRect.topRight();
            start.rx() -= 3;
            horizontalGradTop.rx() -= optRect.width() - 3;
        }
        QLinearGradient gradient(start, horizontalGradTop);
        gradient.setColorAt(0, outlineColor);
        gradient.setColorAt(1, option.palette.base().color());
        QSize rectSize;
        if (leftToRight) {
            rectSize = QSize(optRect.width() - 3, 1);
        } else {
            rectSize = QSize(-optRect.width() + 3, 1);
        }
        painter->fillRect(QRect(start, rectSize), gradient);
    }
    // END: horizontal line

    // BEGIN: draw text
    {
        const QString category = index.model()->data(index, Qt::DisplayRole).toString(); // KCategorizedSortFilterProxyModel::CategoryDisplayRole).toString();
        QRect textRect = QRect(option.rect.topLeft(), QSize(option.rect.width() - 2 - 3 - 3, height));
        textRect.setTop(textRect.top() + 2 + 3 /* corner */);
        textRect.setLeft(textRect.left() + 2 + 3 /* corner */ + 3 /* a bit of margin */);
        painter->save();
        painter->setFont(font);
        QColor penColor(option.palette.text().color());
        penColor.setAlphaF(0.6);
        painter->setPen(penColor);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignTop, category);
        painter->restore();
    }
    // END: draw text
}

int KateCategoryDrawer::categoryHeight(const QModelIndex &index, const QStyleOption &option) const
{
    Q_UNUSED(index);
    Q_UNUSED(option);

    QFont font(QApplication::font());
    font.setBold(true);
    const QFontMetrics fontMetrics = QFontMetrics(font);

    return fontMetrics.height() + 2 + 12 /* vertical spacing */;
}

int KateCategoryDrawer::leftMargin() const
{
    return 7;
}

int KateCategoryDrawer::rightMargin() const
{
    return 7;
}
