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

import java.nio.ByteBuffer;

public class CoolCache {

    static private CoolBufferCache<ByteBuffer> byteBufferCache;
    static private CoolBufferCache<byte[]> byteArrayCache;
    static private CoolCache cache;

    private CoolCache() {
        byteArrayCache = new CoolBufferCache<byte[]>();
    }

    public synchronized static CoolCache getInstance(){
        if(cache == null)
            cache = new CoolCache();
        return cache;
    }

    public synchronized byte[] getByteArray(int desiredLength){
        return null;
    }

    public synchronized void putByteArray(byte[] arr){
    }

    public synchronized ByteBuffer getByteBuffer(int desiredLength) {
        return null;
    }

    public synchronized void putByteBuffer(ByteBuffer buf) {
    }
}
