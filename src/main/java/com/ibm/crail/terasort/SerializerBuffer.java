/*
 * Crail-terasort: An example terasort program for Sprak and crail
 *
 * Author: Animesh Trivedi <atr@zurich.ibm.com>
 *         Jonas Pfefferle <jpf@zurich.ibm.com>
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

public class SerializerBuffer {
    byte[] byteArray = null;
    ByteBuffer byteBuffer = null;
    int allocCapacity = -1;
    int usage = 0;

    public SerializerBuffer(int size, boolean onlyByteArray){
        allocCapacity = size;
        byteArray = new byte[allocCapacity];
        if(!onlyByteArray)
            byteBuffer = ByteBuffer.allocateDirect(allocCapacity);
        usage = 0;
    }

    public final ByteBuffer getByteBuffer(){
        return byteBuffer;
    }
    public final byte[] getByteArray(){
        return byteArray;
    }

    public final void put(){
        if(byteBuffer != null)
            byteBuffer.clear();
        usage--;
    }

    /* prepare buffer for this particular size */
    public final void get(int size){
        if(size > allocCapacity)
            throw new IllegalArgumentException(" not big enough alloc: " + allocCapacity + " askedFor: " + size);
        if(byteBuffer != null)
            byteBuffer.clear();
        usage++;
    }

    public final boolean readyForUse(int size, boolean onlyByteArray){
        boolean sizeCons = (usage == 0 && size <= allocCapacity);
        if(!sizeCons)
            return false;
        /* if we only want byte[] */
        if(onlyByteArray)
            return true;

        /* we want both and one is not allocated */
        if(byteBuffer == null)
            byteBuffer = ByteBuffer.allocateDirect(allocCapacity);

        return true;
    }
}
