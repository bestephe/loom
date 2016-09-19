/*
 * Crail-terasort: An example terasort program for Sprak and crail
 *
 * Author: Animesh Trivedi <atr@zurich.ibm.com>
 *
 * Copyright (C) 2016, IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

package com.ibm.crail.terasort;

import java.util.LinkedList;
import java.util.List;

public class BufferCache {

    private static BufferCache cache = null;
    long totalAccess;
    long missAccess;

    List<SerializerBuffer> serBufferCache = null;

    private BufferCache(){
        serBufferCache = new LinkedList();
        totalAccess = missAccess = 0;
    }
    public synchronized static BufferCache getInstance(){
        if(cache == null)
            cache = new BufferCache();
        return cache;
    }

    public synchronized final SerializerBuffer getUnifedBuffer(int size) {
        return _getUnifedBuffer(size, false);
    }
    public synchronized final void putBuffer(SerializerBuffer buf){
        buf.put();
    }
    public synchronized final SerializerBuffer getByteArrayBuffer(int size) {
        return _getUnifedBuffer(size, true);
    }

    private final SerializerBuffer _getUnifedBuffer(int size, boolean onlyByteArray){
        int sz = serBufferCache.size();
        SerializerBuffer buf;
        totalAccess++;
        for (int i = 0; i < sz; i++) {
            buf = serBufferCache.get(i);
            if(buf.readyForUse(size, onlyByteArray)) {
                buf.get(size);
                return buf;
            }
        }
        missAccess++;
        /* we are at this point where we dont have any buffer */
        buf = new SerializerBuffer(size, onlyByteArray);
        buf.get(size);
        serBufferCache.add(buf);
        return buf;
    }
    public final long getTotalAccess(){
        return totalAccess;
    }
    public final long getMissAccess(){
        return missAccess;
    }
}
