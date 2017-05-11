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

public class SerializerBuffer {
    byte[] byteArray = null;
    int usage = 0;

    public SerializerBuffer(int size){
        byteArray = new byte[size];
        usage = 0;
    }

    public final byte[] getByteArray(){
        return byteArray;
    }

    public final void put(){
        assert (usage == 1);
        usage--;
    }

    public final void get(){
        assert (usage == 0);
        usage++;
    }

    public final boolean readyForUse(int size){
        return (usage == 0 && size <= byteArray.length);
    }
}
