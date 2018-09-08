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

import java.util.Random

import com.google.common.primitives.UnsignedBytes
import com.ibm.crail.terasort.serializer.{ByteSerializer, KryoSerializer}
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat
import org.apache.spark.rdd._
import org.apache.spark.{Partitioner, SparkConf, SparkContext}

object TeraSort {

  val f22BufSizeKey = "spark.terasort.f22buffersize"
  val kryoBufSizeKey = "spark.terasort.kryobuffersize"
  val verboseKey = "spark.terasort.verbose"

  val verbosePrefixF22 = "F22 | "
  val verbosePrefixSorter = "Sorter | "
  val verbosePrefixIterator = "SorterIterator | "
  val verbosePrefixCache = "Cache | "
  val verbosePrefixHDFSInput = "HDFSInput | "
  val verbosePrefixHDFSOutput = "HDFSOutput | "

  val keyOrder = new Ordering[Array[Byte]] {
    override def compare (a:Array[Byte], b:Array[Byte]) = UnsignedBytes.lexicographicalComparator.compare(a,b)
  }

  def main (args: Array[String]) {
    doSortWithOpts(args,None)
  }

  def doSortWithOpts (args: Array[String], scin: Option[SparkContext]) {

    val options = new ParseTeraOptions
    options.parse(args)

    if (options.getF22BufferSize % TeraInputFormat.RECORD_LEN != 0) {
      System.err.println("F22 buffer size (" + options.getF22BufferSize +
        " ) needs to be a multiple of TeraSort record size " + TeraInputFormat.RECORD_LEN)
      System.exit(-1)
    }
    val warnings = new StringBuilder

    println(" ##############################")
    println(options.getBanner)
    options.show_help()
    options.showOptions()
    println(" ##############################")

    val inputFile = options.getInputDir
    val outputFile = options.getOutputDir

    val sc: SparkContext = scin match {
      case Some(scin) =>
        scin
      case None =>
        val conf = new SparkConf()
          .setAppName(s"TeraSort")
        new SparkContext(conf)
    }
    /* now we set the params */
    options.setSparkOptions(sc)
    /* set all the properties that we read downstream in executors */
    sc.setLocalProperty(f22BufSizeKey, options.getF22BufferSize.toString)
    sc.setLocalProperty(kryoBufSizeKey, options.getKryoBufferSize.toString)
    sc.setLocalProperty(verboseKey, options.getVerbose.toString)

    if(options.getWarmUpKeys > 0) {
      doWarmup(sc, options)
    }

    try {
      if(options.isPartitionSet) {
        val splitsize = options.getParitionSize
        sc.hadoopConfiguration.set(FileInputFormat.SPLIT_MINSIZE, splitsize.toString)
        sc.hadoopConfiguration.set(FileInputFormat.SPLIT_MAXSIZE, splitsize.toString)
      }
      /* see if we want to sync the output file */
      sc.hadoopConfiguration.setBoolean(TeraOutputFormat.FINAL_SYNC_ATTRIBUTE,
        options.getSyncOutput)

      val beg = java.lang.System.currentTimeMillis()
      val dataset = sc.newAPIHadoopFile[Array[Byte], Array[Byte], TeraInputFormat](inputFile)
      val setting = options.showOptions()
      println(setting)

      if(options.isTestLoadOnly) {
        println("load test only (triggered by counting) of " + dataset.count() + " records")
      }
      else if(options.isTestLoadStore) {
        println("loadStore test from : " + inputFile + " to: " + outputFile)
        dataset.saveAsNewAPIHadoopFile[TeraOutputFormat](outputFile)
      } else {
        var exe = ""
        val numberReduceTasks = if(options.isReduceTasksSet) options.getNumberOfReduceTasks else dataset.partitions.length
        val partit = new TeraSortPartitionerInt(numberReduceTasks)
        /* check if we need sorting order or not */
        val sorted = new ShuffledRDD[Array[Byte], Array[Byte], Array[Byte]](dataset, partit)
        val setSorting = options.isTestLoadSort || options.isTestLoadSortStore
        if(setSorting) {
          exe += " sort + "
          sorted.setKeyOrdering(keyOrder)
        } else {
          sorted.setKeyOrdering(null)
          exe += " noSort + "
        }
        /* which serializer to use */
        if (options.isSerializerKryo) {
          sorted.setSerializer(new KryoSerializer())
          exe += " serializer : Kryo "
        } else if (options.isSerializerByte) {
          sorted.setSerializer(new ByteSerializer())
          exe += " serializer : byte "
        }else if (options.isSerializerF22) {
          //TODO: printout warning to make sure
          warnings.append("-------------------------------------------\n")
          warnings.append("F22 can only be loaded by conf/spark-defaults.conf file by setting spark.crail.serializer property\n")
          warnings.append("-------------------------------------------\n")
          exe += " serializer : F22 (Warning: F22 can only be loaded by spark-default config) "
        } else {
          exe += " noSerializer + "
        }

        val outrdd = new PairRDDFunctions(sorted)
        if(options.isTestLoadCountStore || options.isTestLoadSortStore) {
          /* store */
          exe += " storing the file at: " + outputFile
          outrdd.saveAsNewAPIHadoopFile[TeraOutputFormat](outputFile)
        } else {
          /* just count */
          if(options.isTestLoadSort)
            exe += " sorting, contains " + outrdd.keys.count() // will trigger the action
          else
            exe += " counting only, contains " + outrdd.keys.count() // will trigger the action
        }
        println("---------- Action Plan --------------------")
        println(exe)
        println("-------------------------------------------")
      }
      val end = java.lang.System.currentTimeMillis()
      println(setting)
      println("-------------------------------------------")
      println("Execution time: %.3fsec".format((end-beg)/1000.0) + " partition size was: " + dataset.partitions.length)
      println("-------------------------------------------")
      println("Warnings (if any)...")
      println(warnings.mkString)
    }
    finally {
      if( scin.isEmpty ) {
        sc.stop()
      }
    }
  }

  def doWarmup (scin: SparkContext, options: ParseTeraOptions): Unit = {

    /* a simple Partitioner. We can also use TeraSortPartitionerInt instead of it */
    case class ShufflePartitioner(numPartitions: Int) extends Partitioner {
      require(numPartitions >= 0, s"Number of partitions ($numPartitions) cannot be negative.")

      def nonNegativeMod(x: Int, mod: Int): Int = {
        val rawMod = x % mod
        rawMod + (if (rawMod < 0) mod else 0)
      }

      /* we expect key to be a byte array - a byte gives us 256 partitions */
      def getPartition(key: Any): Int = key match {
        case null => 0
        case _ => nonNegativeMod(key.asInstanceOf[Array[Byte]](0), numPartitions)
      }

      override def equals(other: Any): Boolean = other match {
        case h: ShufflePartitioner =>
          h.numPartitions == numPartitions
        case _ =>
          false
      }

      override def hashCode: Int = numPartitions
    }

    val outputFile = "/terasort-output-warmup"
    val cores = if(scin.getConf.contains("spark.executor.cores")) scin.getConf.get("spark.executor.cores").toInt else 1
    val exes = if(scin.getConf.contains("spark.executor.instances")) scin.getConf.get("spark.executor.instances").toInt else 2

    val totalWorkers = cores * exes
    val totalKeys = options.getWarmUpKeys

    println("warmup*, executors " + exes + ", cores " + cores + ", totalKeys " + totalKeys)
    /* we want atleast one key per worker */
    val perWorker = if(totalKeys < totalWorkers) 1 else (totalKeys/totalWorkers).asInstanceOf[Int]

    val inx = scin.parallelize(0 until totalWorkers, totalWorkers).flatMap { p =>
      val ranGen = new Random
      val arr1 = new Array[(Array[Byte], Array[Byte])](perWorker)
      for (i <- 0 until perWorker) {
        val key = new Array[Byte](TeraInputFormat.KEY_LEN)
        ranGen.nextBytes(key)
        val value = new Array[Byte](TeraInputFormat.VALUE_LEN)
        arr1(i) = (key, value)
      }
      arr1
    }

    val shuffle = new ShuffledRDD[Array[Byte], Array[Byte], Array[Byte]](inx, new ShufflePartitioner(totalWorkers))
    shuffle.setKeyOrdering(keyOrder)

    if (options.isSerializerKryo) {
      shuffle.setSerializer(new KryoSerializer())
    } else if (options.isSerializerByte) {
      shuffle.setSerializer(new ByteSerializer())
    }
    val out = new PairRDDFunctions(shuffle)
    out.saveAsNewAPIHadoopFile[TeraOutputFormat](outputFile)
  }
}
