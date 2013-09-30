/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include "qv4arraydata_p.h"
#include "qv4object_p.h"
#include "qv4functionobject_p.h"

using namespace QV4;

const ArrayVTable ArrayData::static_vtbl =
{
    ArrayData::Simple,
    ArrayData::freeData,
    ArrayData::reserve,
    ArrayData::get,
    ArrayData::put,
    ArrayData::putArray,
    ArrayData::del,
    ArrayData::setAttribute,
    ArrayData::attribute,
    ArrayData::push_front,
    ArrayData::pop_front,
    ArrayData::truncate
};

const ArrayVTable SparseArrayData::static_vtbl =
{
    ArrayData::Sparse,
    SparseArrayData::freeData,
    SparseArrayData::reserve,
    SparseArrayData::get,
    SparseArrayData::put,
    SparseArrayData::putArray,
    SparseArrayData::del,
    SparseArrayData::setAttribute,
    SparseArrayData::attribute,
    SparseArrayData::push_front,
    SparseArrayData::pop_front,
    SparseArrayData::truncate
};


void ArrayData::getHeadRoom(ArrayData *d)
{
    Q_ASSERT(d);
    Q_ASSERT(!d->offset);
    d->offset = qMax(d->len >> 2, (uint)16);
    Property *newArray = new Property[d->offset + d->alloc];
    memcpy(newArray + d->offset, d->data, d->len*sizeof(Property));
    delete [] d->data;
    d->data = newArray + d->offset;
    if (d->attrs) {
        PropertyAttributes *newAttrs = new PropertyAttributes[d->offset + d->alloc];
        memcpy(newAttrs + d->offset, d->attrs, d->len*sizeof(PropertyAttributes));
        delete [] d->attrs;
        d->attrs = newAttrs + d->offset;
    }
}

void ArrayData::reserve(ArrayData *d, uint n)
{
    if (n < 8)
        n = 8;
    if (n <= d->alloc)
        return;

    d->alloc = qMax(n, 2*d->alloc);
    Property *newArrayData = new Property[d->alloc + d->offset];
    if (d->data) {
        memcpy(newArrayData + d->offset, d->data, sizeof(Property)*d->len);
        delete [] (d->data - d->offset);
    }
    d->data = newArrayData + d->offset;

    if (d->attrs) {
        PropertyAttributes *newAttrs = new PropertyAttributes[d->alloc];
        memcpy(newAttrs, d->attrs, sizeof(PropertyAttributes)*d->len);
        delete [] (d->attrs - d->offset);

        d->attrs = newAttrs;
    }
}

void ArrayData::ensureAttributes()
{
    if (attrs)
        return;

    if (type == Simple)
        type = Complex;
    attrs = new PropertyAttributes[alloc + offset];
    attrs += offset;
    for (uint i = 0; i < len; ++i)
        attrs[i] = Attr_Data;
}


void ArrayData::freeData(ArrayData *d)
{
    delete [] (d->data - d->offset);
    if (d->attrs)
        delete [] (d->attrs - d->offset);
    delete d;
}

ReturnedValue ArrayData::get(const ArrayData *d, uint index)
{
    if (index >= d->len)
        return Primitive::emptyValue().asReturnedValue();
    return d->data[index].value.asReturnedValue();
}

bool ArrayData::put(ArrayData *d, uint index, ValueRef value)
{
    Q_ASSERT(!d->attrs || !d->attrs->isAccessor());
    // ### honour attributes
    d->data[index].value = value;
    return true;
}

bool ArrayData::del(ArrayData *d, uint index)
{
    if (index >= d->len)
        return true;

    if (!d->attrs || d->attrs[index].isConfigurable()) {
        d->data[index].value = Primitive::emptyValue();
        if (d->attrs)
            d->attrs[index] = Attr_Data;
        return true;
    }
    if (d->data[index].value.isEmpty())
        return true;
    return false;
}

void ArrayData::setAttribute(ArrayData *d, uint index, PropertyAttributes attrs)
{
    d->attrs[index] = attrs;
}

PropertyAttributes ArrayData::attribute(const ArrayData *d, uint index)
{
    return d->attrs[index];
}

void ArrayData::push_front(ArrayData *d, SafeValue *values, uint n)
{
    Q_ASSERT(!d->attrs);
    for (int i = n - 1; i >= 0; --i) {
        if (!d->offset)
            ArrayData::getHeadRoom(d);

        --d->offset;
        --d->data;
        ++d->len;
        ++d->alloc;
        d->data->value = values[i].asReturnedValue();
    }

}

ReturnedValue ArrayData::pop_front(ArrayData *d)
{
    Q_ASSERT(!d->attrs);
    if (!d->len)
        return Encode::undefined();

    ReturnedValue v = d->data[0].value.isEmpty() ? Encode::undefined() : d->data[0].value.asReturnedValue();
    ++d->offset;
    ++d->data;
    --d->len;
    --d->alloc;
    return v;
}

uint ArrayData::truncate(ArrayData *d, uint newLen)
{
    if (d->attrs) {
        Property *it = d->data + d->len;
        const Property *begin = d->data + newLen;
        while (--it >= begin) {
            if (!it->value.isEmpty() && !d->attrs[it - d->data].isConfigurable()) {
                newLen = it - d->data + 1;
                break;
            }
            it->value = Primitive::emptyValue();
        }
    }
    d->len = newLen;
    return newLen;
}

bool ArrayData::putArray(ArrayData *d, uint index, SafeValue *values, uint n)
{
    if (index + n > d->alloc)
        reserve(d, index + n + 1);
    for (uint i = d->len; i < index; ++i)
        d->data[i].value = Primitive::emptyValue();
    for (uint i = 0; i < n; ++i)
        d->data[index + i].value = values[i];
    d->len = qMax(d->len, index + n);
    return true;
}

void SparseArrayData::free(ArrayData *d, uint idx)
{
    Q_ASSERT(d && d->type == ArrayData::Sparse);
    SparseArrayData *dd = static_cast<SparseArrayData *>(d);
    Property &pd = dd->data[idx];
    pd.value.uint_32 = dd->freeList;
    dd->freeList = idx;
    if (dd->attrs)
        dd->attrs[idx].clear();
}


void SparseArrayData::freeData(ArrayData *d)
{
    delete static_cast<SparseArrayData *>(d)->sparse;
    ArrayData::freeData(d);
}

void SparseArrayData::reserve(ArrayData *d, uint n)
{
    if (n < 8)
        n = 8;
    if (n <= d->alloc)
        return;

    SparseArrayData *dd = static_cast<SparseArrayData *>(d);
    // ### FIXME
    dd->len = dd->alloc;
    dd->alloc = qMax(n, 2*dd->alloc);
    Property *newArrayData = new Property[dd->alloc];
    if (dd->data) {
        memcpy(newArrayData, dd->data, sizeof(Property)*dd->len);
        delete [] dd->data;
    }
    dd->data = newArrayData;
    if (dd->attrs) {
        PropertyAttributes *newAttrs = new PropertyAttributes[dd->alloc];
        memcpy(newAttrs, dd->attrs, sizeof(PropertyAttributes)*dd->len);
        delete [] dd->attrs;
        dd->attrs = newAttrs;
    }
    for (uint i = dd->freeList; i < dd->alloc; ++i)
        dd->data[i].value = Primitive::fromInt32(i + 1);
}

uint SparseArrayData::allocate(ArrayData *d)
{
    Q_ASSERT(d->type == ArrayData::Sparse);
    SparseArrayData *dd = static_cast<SparseArrayData *>(d);
    uint idx = dd->freeList;
    if (dd->alloc == dd->freeList)
        reserve(d, d->alloc + 1);
    dd->freeList = dd->data[dd->freeList].value.uint_32;
    if (dd->attrs)
        dd->attrs[idx].setType(PropertyAttributes::Data);
    return idx;
}

ReturnedValue SparseArrayData::get(const ArrayData *d, uint index)
{
    SparseArrayNode *n = static_cast<const SparseArrayData *>(d)->sparse->findNode(index);
    if (!n)
        return Primitive::emptyValue().asReturnedValue();
    return d->data[n->value].value.asReturnedValue();
}

bool SparseArrayData::put(ArrayData *d, uint index, ValueRef value)
{
    // ### honour attributes
    SparseArrayNode *n = static_cast<SparseArrayData *>(d)->sparse->insert(index);
    if (n->value == UINT_MAX)
        n->value = allocate(d);
    d->data[n->value].value = value;
    return true;
}

bool SparseArrayData::del(ArrayData *d, uint index)
{
    SparseArrayData *dd = static_cast<SparseArrayData *>(d);
    SparseArrayNode *n = dd->sparse->findNode(index);
    if (!n)
        return true;

    uint pidx = n->value;
    Q_ASSERT(!dd->data[pidx].value.isEmpty());

    if (!dd->attrs || dd->attrs[pidx].isConfigurable()) {
        d->data[pidx].value.int_32 = static_cast<SparseArrayData *>(d)->freeList;
        static_cast<SparseArrayData *>(d)->freeList = pidx;
        static_cast<SparseArrayData *>(d)->sparse->erase(n);
        return true;
    }
    return false;
}

void SparseArrayData::setAttribute(ArrayData *d, uint index, PropertyAttributes attrs)
{
    SparseArrayNode *n = static_cast<SparseArrayData *>(d)->sparse->insert(index);
    if (n->value == UINT_MAX)
        n->value = allocate(d);
    d->attrs[n->value] = attrs;
}

PropertyAttributes SparseArrayData::attribute(const ArrayData *d, uint index)
{
    SparseArrayNode *n = static_cast<const SparseArrayData *>(d)->sparse->insert(index);
    if (!n)
        return PropertyAttributes();
    return d->attrs[n->value];
}

void SparseArrayData::push_front(ArrayData *d, SafeValue *values, uint n)
{
    Q_ASSERT(!d->attrs);
    for (int i = n - 1; i >= 0; --i) {
        uint idx = allocate(d);
        d->data[idx].value = values[i];
        static_cast<SparseArrayData *>(d)->sparse->push_front(idx);
    }
}

ReturnedValue SparseArrayData::pop_front(ArrayData *d)
{
    Q_ASSERT(!d->attrs);
    uint idx = static_cast<SparseArrayData *>(d)->sparse->pop_front();
    ReturnedValue v;
    if (idx != UINT_MAX) {
        v = d->data[idx].value.asReturnedValue();
        SparseArrayData::free(d, idx);
    } else {
        v = Encode::undefined();
    }
    return v;
}

uint SparseArrayData::truncate(ArrayData *d, uint newLen)
{
    SparseArrayNode *begin = static_cast<SparseArrayData *>(d)->sparse->lowerBound(newLen);
    if (begin != static_cast<SparseArrayData *>(d)->sparse->end()) {
        SparseArrayNode *it = static_cast<SparseArrayData *>(d)->sparse->end()->previousNode();
        while (1) {
            Property &pd = d->data[it->value];
            if (d->attrs) {
                if (!d->attrs[it->value].isConfigurable()) {
                    newLen = it->key() + 1;
                    break;
                }
            }
            pd.value.tag = Value::Empty_Type;
            pd.value.int_32 = static_cast<SparseArrayData *>(d)->freeList;
            static_cast<SparseArrayData *>(d)->freeList = it->value;
            bool brk = (it == begin);
            SparseArrayNode *prev = it->previousNode();
            static_cast<SparseArrayData *>(d)->sparse->erase(it);
            if (brk)
                break;
            it = prev;
        }
    }
    return newLen;
}

bool SparseArrayData::putArray(ArrayData *d, uint index, SafeValue *values, uint n)
{
    for (uint i = 0; i < n; ++i)
        put(d, index + i, values[i]);
    d->len = qMax(d->len, index + n);
    return true;
}


uint ArrayData::append(Object *o, const ArrayObject *otherObj, uint n)
{
    ArrayData *d = o->arrayData;
    if (!n)
        return d->len;

    const ArrayData *other = otherObj->arrayData;

    if (other->isSparse()) {
        o->initSparseArray();
        d = o->arrayData;
    }

    uint oldSize = d->len;

    // ### copy attributes as well!
    if (d->type == ArrayData::Sparse) {
        if (other->isSparse()) {
            for (const SparseArrayNode *it = static_cast<const SparseArrayData *>(other)->sparse->begin();
                 it != static_cast<const SparseArrayData *>(other)->sparse->end(); it = it->nextNode())
                // ### accessor properties
                o->arraySet(d->len + it->key(), other->data[it->value].value);
        } else {
            d->vtable->reserve(d, oldSize + n);
            memcpy(d->data + oldSize, other->data, n*sizeof(Property));
            if (d->attrs)
                std::fill(d->attrs + oldSize, d->attrs + oldSize + n, PropertyAttributes(Attr_Data));
            for (uint i = 0; i < n; ++i) {
                SparseArrayNode *n = static_cast<SparseArrayData *>(d)->sparse->insert(d->len + i);
                n->value = oldSize + i;
            }
        }
    } else if (other->length()) {
        d->vtable->reserve(d, oldSize + other->length());
        if (oldSize > d->len) {
            for (uint i = d->len; i < oldSize; ++i)
                d->data[i].value = Primitive::emptyValue();
        }
        if (other->attrs) {
            for (uint i = 0; i < other->len; ++i) {
                bool exists;
                d->data[oldSize + i].value = const_cast<ArrayObject *>(otherObj)->getIndexed(i, &exists);
                d->len = oldSize + i + 1;
                o->arrayData->setAttributes(oldSize + i, Attr_Data);
                if (!exists)
                    d->data[oldSize + i].value = Primitive::emptyValue();
            }
        } else {
            d->len = oldSize + other->len;
            memcpy(d->data + oldSize, other->data, other->len*sizeof(Property));
            if (d->attrs)
                std::fill(d->attrs + oldSize, d->attrs + oldSize + other->len, PropertyAttributes(Attr_Data));
        }
    }

    return oldSize + n;
}

Property *ArrayData::insert(Object *o, uint index)
{
    Property *pd;
    if (o->arrayData->type != ArrayData::Sparse && (index < 0x1000 || index < o->arrayData->len + (o->arrayData->len >> 2))) {
        if (index >= o->arrayData->alloc)
            o->arrayReserve(index + 1);
        if (index >= o->arrayData->len) {
            // mark possible hole in the array
            for (uint i = o->arrayData->len; i < index; ++i)
                o->arrayData->data[i].value = Primitive::emptyValue();
            o->arrayData->len = index + 1;
        }
        pd = o->arrayData->data + index;
    } else {
        o->initSparseArray();
        SparseArrayNode *n = static_cast<SparseArrayData *>(o->arrayData)->sparse->insert(index);
        if (n->value == UINT_MAX)
            n->value = SparseArrayData::allocate(o->arrayData);
        pd = o->arrayData->data + n->value;
    }
    return pd;
}

void ArrayData::markObjects(ExecutionEngine *e)
{
    if (type == ArrayData::Simple) {
        for (uint i = 0; i < len; ++i)
            data[i].value.mark(e);
        return;
    } else {
        for (uint i = 0; i < len; ++i) {
            const Property &pd = data[i];
            if (attrs && attrs[i].isAccessor()) {
                if (pd.getter())
                    pd.getter()->mark(e);
                if (pd.setter())
                    pd.setter()->mark(e);
            } else {
                pd.value.mark(e);
            }
        }
    }

}

void ArrayData::sort(ExecutionContext *context, ObjectRef thisObject, const ValueRef comparefn, uint len)
{
    ArrayData *d = thisObject->arrayData;
    if (!d || !d->len)
        return;

    if (d->type == ArrayData::Sparse) {
        context->throwUnimplemented(QStringLiteral("Object::sort unimplemented for sparse arrays"));
        return;
    }

    if (len > d->len)
        len = d->len;

    // The spec says the sorting goes through a series of get,put and delete operations.
    // this implies that the attributes don't get sorted around.
    // behavior of accessor properties is implementation defined. We simply turn them all
    // into data properties and then sort. This is in line with the sentence above.
    if (d->attrs) {
        for (uint i = 0; i < len; i++) {
            if (d->data[i].value.isEmpty()) {
                while (--len > i)
                    if (!d->data[len].value.isEmpty())
                        break;
                d->data[i].value = thisObject->getValue(d->data + len, d->attrs ? d->attrs[len] : Attr_Data);
                if (d->attrs)
                    d->attrs[i] = Attr_Data;
                d->data[len].value = Primitive::emptyValue();
            } else if (d->attrs && d->attrs[i].isAccessor()) {
                d->data[i].value = thisObject->getValue(d->data + i, d->attrs[i]);
                d->attrs[i] = Attr_Data;
            }
        }
    }

    if (!(comparefn->isUndefined() || comparefn->asObject())) {
        context->throwTypeError();
        return;
    }

    ArrayElementLessThan lessThan(context, thisObject, comparefn);

    if (!len)
        return;
    Property *begin = d->data;
    std::sort(begin, begin + len, lessThan);
}