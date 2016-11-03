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

import org.apache.hadoop.fs.{FSDataOutputStream, FileSystem, Path}
import org.apache.hadoop.mapred.InvalidJobConfException
import org.apache.hadoop.mapreduce.{JobContext, OutputCommitter, RecordWriter, TaskAttemptContext}
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat
import org.apache.hadoop.mapreduce.lib.output.{FileOutputCommitter, FileOutputFormat}
import org.apache.hadoop.mapreduce.security.TokenCache
import org.apache.spark.TaskContext

object TeraOutputFormat {
  val FINAL_SYNC_ATTRIBUTE = "mapreduce.terasort.final.sync"
  val OUTDIR = "mapreduce.output.fileoutputformat.outputdir"
}

class TeraOutputFormat extends FileOutputFormat[Array[Byte], Array[Byte]] {
  var committer : OutputCommitter = null

  /**
    * Set the requirement for a final sync before the stream is closed.
    */
  def setFinalSync(job : JobContext, newValue : Boolean) =
    job.getConfiguration.setBoolean(TeraOutputFormat.FINAL_SYNC_ATTRIBUTE, newValue)

  /**
    * Does the user want a final sync at close?
    */
  def getFinalSync(job : JobContext ) : Boolean =
    job.getConfiguration.getBoolean(TeraOutputFormat.FINAL_SYNC_ATTRIBUTE,
      false)

  class TeraRecordWriter(val out : FSDataOutputStream, val job: JobContext, bufSize:Int)
    extends RecordWriter[Array[Byte], Array[Byte]] {

    private val finalSync = getFinalSync(job)
    private val start = System.nanoTime()
    private val cacheBuffer = BufferCache.getInstance().getByteArrayBuffer(bufSize)
    private val byteBuffer = cacheBuffer.getByteArray
    private var copied = 0
    private val verbose = TaskContext.get().getLocalProperty(TeraSort.verboseKey).toBoolean
    private var totalOutputBytes = 0
    if(verbose) {
      System.err.println(TeraSort.verbosePrefixHDFSOutput + " TID: " + TaskContext.get.taskAttemptId() +
        " sync flag is : " + finalSync + " and output buffer size : " + bufSize)
    }

    final def require(bytes: Int): Unit = {
      if(copied + bytes > bufSize) {
        totalOutputBytes+=copied
        /* we flush here */
        out.write(byteBuffer, 0, copied)
        copied = 0
      }
    }

    def write(key : Array[Byte], value : Array[Byte]) = {
        /* we see if we can fit all */
        require(key.length + value.length)
        /* then we copy it out */
        System.arraycopy(key, 0, byteBuffer, copied, key.length)
        copied+=key.length
        System.arraycopy(value, 0, byteBuffer, copied, value.length)
        copied+=value.length
    }

    def close(context : TaskAttemptContext) = {
      /* unconditional flush here */
      totalOutputBytes+=copied
      out.write(byteBuffer, 0, copied)
      copied = 0
      /* put buffer back */
      BufferCache.getInstance().putBuffer(cacheBuffer)
      /* wait for sync */
      if (finalSync) {
        out.hsync()
      }
      /* close the stream */
      out.close()
      if(verbose) {
        val end = System.nanoTime()
        System.err.println(TeraSort.verbosePrefixHDFSOutput + " TID: " + TaskContext.get.taskAttemptId() +
          " finished writing " + totalOutputBytes + " bytes, " +
          BufferCache.getInstance.getCacheStatus + " jobtime: " + (end - start) / 1000 + " usec")
      }
    }
  }

  override def checkOutputSpecs(job : JobContext) = {
    // Ensure that the output directory is set
    val outDir : Path = getOutputPath(job)
    if (outDir == null) {
      throw new InvalidJobConfException("Output directory not set in JobConf.")
    }

    // get delegation token for outDir's file system
    TokenCache.obtainTokensForNamenodes(job.getCredentials,
      Array[Path](outDir), job.getConfiguration)
  }

  /*
  Backported from Hadoop FileOutputPath from later versions than 1.0.4
   */
  def getOutputPath(job : JobContext ) : Path =  {
    job.getConfiguration.get(TeraOutputFormat.OUTDIR) match {
      case null => null
      case name => new Path(name)
    }
  }

  def getRecordWriter(job : TaskAttemptContext)
  : RecordWriter[Array[Byte], Array[Byte]] = {
    val file : Path = getDefaultWorkFile(job, "")
    val fs : FileSystem = file.getFileSystem(job.getConfiguration)
    fs.setVerifyChecksum(false)
    fs.setWriteChecksum(false)
    val fileOut : FSDataOutputStream = fs.create(file)

    /* get the input partition size - good estimate of output data as well */
    val minSplit = job.getConfiguration.get(FileInputFormat.SPLIT_MINSIZE).toLong
    //val maxSplit = job.getConfiguration.get(FileInputFormat.SPLIT_MINSIZE)
    val blockSize = fs.getConf().get("dfs.blocksize").toLong
    /* we always set both, min and max split, hence they must be equal */
    val preferredBufferSize = if(minSplit == 0) blockSize else minSplit
    new TeraRecordWriter(fileOut, job, preferredBufferSize.toInt)
  }

  override def getOutputCommitter(context : TaskAttemptContext) : OutputCommitter = {
    if (committer == null) {
      val output = getOutputPath(context)
      committer = new FileOutputCommitter(output, context)
    }
    committer
  }
}
