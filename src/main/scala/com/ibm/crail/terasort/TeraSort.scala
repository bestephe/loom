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

import com.google.common.primitives.UnsignedBytes
import com.ibm.crail.terasort.serializer.{F22Serializer, ByteSerializer, KryoSerializer, F16Serializer}
import org.apache.spark.rdd._
import org.apache.spark.{SparkConf, SparkContext}
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat

object TeraSort {

  val keyOrder = new Ordering[Array[Byte]] {
    override def compare (a:Array[Byte], b:Array[Byte]) = UnsignedBytes.lexicographicalComparator.compare(a,b)
  }

  def main (args: Array[String]) {
    doSortWithOpts(args,None)
  }

  def doSortWithOpts (args: Array[String], scin: Option[SparkContext]) {
    val options = new ParseTeraOptions
    options.parse(args)

    println(" ##############################")
    println(" This is the test code that take parameters.")
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
    options.setSparkOptions(sc.getConf)

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
        val partit = new TeraSortPartitionerInt(dataset.partitions.size)
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
          sorted.setSerializer(new KryoSerializer(options))
          exe += " serializer : Kryo "
        } else if (options.isSerializerByte) {
          sorted.setSerializer(new ByteSerializer())
          exe += " serializer : byte "
        }else if (options.isSerializerF16) {
          sorted.setSerializer(new F16Serializer())
          exe += " serializer : F16 "
        }else if (options.isSerializerF22) {
          sorted.setSerializer(new F22Serializer())
          //TODO: printout warning to make sure
          exe += " serializer : F22 "
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
      println("Execution time: %.3fsec".format((end-beg)/1000.0) + " partition size was: " + dataset.partitions.size)
      println("-------------------------------------------")
    }
    finally {
      if( scin.isEmpty ) {
        sc.stop()
      }
    }
  }
}
