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

package com.ibm.crail.terasort

import java.io.EOFException
import java.util.List

import org.apache.hadoop.fs.{FSDataInputStream, FileStatus, FileSystem, Path}
import org.apache.hadoop.mapreduce.{InputSplit, JobContext, RecordReader, TaskAttemptContext}
import org.apache.hadoop.mapreduce.lib.input.{FileInputFormat, FileSplit}
import org.apache.spark.TaskContext

import scala.collection.JavaConversions._

object TeraInputFormat {
  val KEY_LEN = 10
  val VALUE_LEN = 90
  val RECORD_LEN = KEY_LEN + VALUE_LEN
}

class TeraInputFormat extends FileInputFormat[Array[Byte], Array[Byte]] {

  override final def createRecordReader(split: InputSplit, context: TaskAttemptContext)
  : RecordReader[Array[Byte], Array[Byte]] = new TeraRecordReader()

  override final def listStatus(job: JobContext): List[FileStatus] = {
    val listing = super.listStatus(job)
    val sortedListing= listing.sortWith{ (lhs, rhs) => {
      lhs.getPath().compareTo(rhs.getPath()) < 0
    } }
    sortedListing.toList
  }

  class TeraRecordReader extends RecordReader[Array[Byte], Array[Byte]] {
    private var in : FSDataInputStream = null
    private var offset: Long = 0
    private var length: Long = 0
    private var key: Array[Byte] = null
    private var value: Array[Byte] = null

    private var byteArray: Array[Byte] = null
    private var serBuffer: SerializerBuffer = null
    private var incomingBytes:Int = 0
    private var copiedSoFar:Int = 0
    private var hitEnd:Boolean = false

    private def showStats(): Unit = {
      val ctx = TaskContext.get
      val stageId = ctx.stageId
      val partId = ctx.partitionId
      //val hostname = ctx.taskMetrics.
      println(s"Stage: $stageId, Partition: $partId")
    }

    //this function must be called just once !!
    private def fillUpBigBuffer(): Unit = {
      val start = System.nanoTime()
      var read : Int = 0
      var newRead : Int = -1
      while (read < incomingBytes) {
        newRead = in.read(byteArray, read, incomingBytes - read)
        if (newRead == -1) {
          if (read == 0)
            hitEnd = true
          else throw new EOFException("read past eof")
        }
        read += newRead
      }
      /* reset used */
      copiedSoFar = 0
      val timeUs = (System.nanoTime() - start)/1000
      System.err.println("TS: TID: " + TaskContext.get.taskAttemptId() +
        " HDFS read bytes: " + incomingBytes +
        " time : " + timeUs  + " usec , or " +
        (incomingBytes.asInstanceOf[Long] * 8)/timeUs + " Mbps")
    }

    override final def nextKeyValue() : Boolean = {
      if (copiedSoFar >= incomingBytes) {
        return false
      }
      /* now we have to copy out things */
      System.arraycopy(byteArray, copiedSoFar, key, 0, TeraInputFormat.KEY_LEN)
      copiedSoFar += TeraInputFormat.KEY_LEN
      System.arraycopy(byteArray, copiedSoFar, value,  0, TeraInputFormat.VALUE_LEN)
      copiedSoFar += TeraInputFormat.VALUE_LEN
      true
    }

    override final def initialize(split : InputSplit, context : TaskAttemptContext) = {
      val reclen = TeraInputFormat.RECORD_LEN
      val fileSplit = split.asInstanceOf[FileSplit]
      val p : Path = fileSplit.getPath
      val fs : FileSystem = p.getFileSystem(context.getConfiguration)
      fs.setVerifyChecksum(false)
      in = fs.open(p)
      // find the offset to start at a record boundary
      offset = (reclen - (fileSplit.getStart % reclen)) % reclen
      val start = offset + fileSplit.getStart
      in.seek(start)
      //check how much is available in the file, with a full multiple this should be good
      length = Math.min(fileSplit.getLength, in.available()) - offset
      val rem = (start + length)%TeraInputFormat.RECORD_LEN
      val endOffset = if(rem == 0) start + length else (start + length + (TeraInputFormat.RECORD_LEN - rem))
      incomingBytes = (endOffset - start).toInt

      require((incomingBytes % TeraInputFormat.RECORD_LEN) == 0 ,
        " incomingBytes did not alight : " + incomingBytes + " mod : " + (incomingBytes % TeraInputFormat.RECORD_LEN))

      if (key == null) {
        key = new Array[Byte](TeraInputFormat.KEY_LEN)
      }
      if (value == null) {
        value = new Array[Byte](TeraInputFormat.VALUE_LEN)
      }
      serBuffer = BufferCache.getInstance().getByteArrayBuffer(incomingBytes)
      byteArray = serBuffer.getByteArray
      fillUpBigBuffer
    }


    override final def close() = {
      byteArray = null
      BufferCache.getInstance().putBuffer(serBuffer)
      in.close()
      System.err.println("TS: TID: " + TaskContext.get.taskAttemptId() + " finished ")
    }
    override final def getCurrentKey : Array[Byte] = key
    override final def getCurrentValue : Array[Byte] = value
    override final def getProgress : Float = copiedSoFar / incomingBytes
  }

}
