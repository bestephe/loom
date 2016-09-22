# Crail-terasort 

Crail-terasort is a modified version of the original Terasort program 
from https://github.com/ehiggs/spark-terasort. This program generates, 
sorts, and validates 100 bytes (10b + 90b) key-value records as described 
by http://sortbenchmark.org/.

This version is optimized to work with the crail file system 
(https://github.com/zrlio/crail). 

The crail-terasort program takes following options :
```bash
-n,--testname <string>            Name of the test valid tests are :
                                  1. loadOnly: load and counts the input dataset
                                  2. loadStore: load the input dataset and stores it
                                  3. loadCount: load, shuffle, and then count the 
                                      resulting dataset
                                  4. loadCountStore: load, shuffle, count, and then 
                                      store the resulting dataset
                                  5. loadSort: load, shuffle, and then sort on key 
                                      the resulting dataset
                                  6. loadSortStore: load, shuffle, sort on key, then 
                                      store the resulting dataset 
                                  the default is : loadSortStore
-i,--inputDir <string>            Name of the input directory
-o,--outputDir <string>           Name of the output directory
-S,--sync <int>                   Takes 0 or 1 to pass to the sync call to the output 
                                  FS while writing (default: 0)
-p,--partitionSize <long>         Partition size, takes k,m,g,t suffixes
                                  (default: input partition size, HDFS has 128MB)
-s,--useSerializer <string>       You can use following serializers:
                                  none : uses the Spark default serializer 
                                  kryo : optimized Kryo for TeraSort 
                                  byte : a simple byte[] serializer 
                                  f16  : an simple byte[] serializer for crail
                                  f22  : an optimized crail-specific byte[] serializer
                                       f22 requires CrailShuffleNativeRadixSorter
-b,--bufferSize <int>             Buffer size for Kryo (only valid for kryo)
-O --options <string,string>      Sets properties on the Spark context. The first 
                                  string is the key, and the second is the value
-h,--help                         Show this help
```

## Building Crail-terasort 
Apart from the dependencies of the original program, crail-terasort 
depends upon 
  - crail-client (1.0) : https://github.com/zrlio/crail
  - spark-io (1.0) : https://github.com/zrlio/spark-io
  - spark (2.0.0)

`spark-io` extensions enable spark to run on the crail file system. 
Hence, you must build and install zrlio artifacts in your local maven 
repository. For details see their respective github repositories. 

After that, compilation of crail-terasort is simple: 

`mvn -DSkipTests -T 1C install` 

After a successful compilation you should have `crail-terasort-2.0.jar` 
in your `./target` folder. This is the crail-terasort app jar. 

### Building libjsort for f22 serializer (optional) 

**Note:** *These steps are optional and only for if you plan to use 
`f22` serializer. However, please note that the best performance is 
only delivered by the combination of `f22` with `CrailShuffleNativeRadixSorter`.*


The f22 serializer requires the use of `CrailShuffleNativeRadixSorter` 
as the shuffle sorter. This sorter has dependencies on a native library 
`libjsort` (which builds on `libboost`). You can find source of 
`libjsort` is in your cloned repository and build by :
```bash 
cmake .
make
```
These steps should give you '`libjsort.so` in your directory. Then you 
should copy the file in your _java_library_directory_ as defined in 
the `$HADOOP_HOME/etc/hadoop/yarn-env.sh`. As an example, we have : 
```bash
export JAVA_LIBRARY_PATH=/path/to/libjsort/:$JAVA_LIBRARY_PATH
```

and set `yarn-site.xml` accordingly to something appropriate:
```xml
 <property>
        <name>yarn.nodemanager.admin-env</name>
        <value>LD_LIBRARY_PATH=/path/to/libjsort/:</value>
  </property>
```

## Running

We assume that you have a working spark installation with `spark-io` 
extensions. Here we give detail instructions about how to generate, 
sort, and validate crail-terasort program. 

### Generating data 

We recommend to use the original Terasort program from Ewan Higgs 
to generate data : https://github.com/ehiggs/spark-terasort. Clone it, 
then build it by simply executing `mvn -DSkipTests -T 1C install`. 
However the original program is written for an older version of spark. 
Hence, we recommend you modify the `pom.xml` file to update the 
versions as : 
```
diff --git a/pom.xml b/pom.xml
index e15c3e1..996973d 100644
--- a/pom.xml
+++ b/pom.xml
@@ -218,9 +218,9 @@
 
 <properties>
   <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>
-  <scala.version>2.10.4</scala.version>
-  <scala.binary.version>2.10</scala.binary.version>
-  <spark.version>1.2.1</spark.version>
+  <scala.version>2.11.8</scala.version>
+  <scala.binary.version>2.11</scala.binary.version>
+  <spark.version>2.0.0</spark.version>
 </properties>
 
 <dependencies>
```

Or alternatively, simply clone the updated repository 
from https://github.com/animeshtrivedi/spark-terasort
  

To generate data use the following command (adjust according to your 
config): 

```bash
./bin/spark-submit -v \
--num-executors 10 --executor-cores 1 --executor-memory 4G \
--master yarn --class com.github.ehiggs.spark.terasort.TeraGen \
path/to/your/sprak-terasort/target/spark-terasort-1.0.jar \
1g /terasort-input-1g
```
 
**Recommendation**: We recommend to use the same number of executors 
and partitions as you have number of machines. For example in the above 
command, we use it to generate `1g` of dataset with `10` executors, 
each with `1` core, with `10` partitions. The number of partitions 
come from `spark.default.parallelism` from the `$SPARK_HOME/conf/spark-defaults.conf`
file. We have it set as `10` for `10` machines. The output directory 
name is specified as `/terasort-input-1g`.

if you experience issues about missing dependencies, then try using 
`spark-terasort-1.0-jar-with-dependencies.jar` from the `target` folder. 


### Sorting data using crail-terasort

After generating data, you can use the following command to run 
crail-terasort app (adjust according to your config):
```bash
./bin/spark-submit -v \
--num-executors 10 --executor-cores 8 --executor-memory 8G --driver-memory 8G\
--master yarn --class com.ibm.crail.terasort.TeraSort \
path/to/your/crail-terasort/target/crail-terasort-2.0.jar \
-i /terasort-input-10g -o /terasort-output-10g
```
The default serializer is none, which means that whatever you use 
(`spark.serializer`) in `$SPARK_HOME/conf/spark-defaults.conf` will be 
used. Once you have the basic setup working, you can try using 
`kryo`, `byte`, or `f16`. 

Using `f22` serializer requires you to set crail-terasort specific 
sorter as well. You must set 
```
spark.crail.shuffle.sorter     com.ibm.crail.terasort.sorter.CrailShuffleNativeRadixSorter
spark.crail.shuffle.serializer com.ibm.crail.terasort.serializer.F22Serializer
```
in your `$SPARK_HOME/conf/spark-defaults.conf`.

**Note**: The f22 serializer is *only* compatible with CrailShuffleNativeRadixSorter!

### Validating the output 

Like input, for output validation we recommend to use the original 
program as (adjust according to your config): 

```bash
./bin/spark-submit -v \
--num-executors 10 --executor-cores 8 --executor-memory 4G \
--master yarn --class com.github.ehiggs.spark.terasort.TeraValidate \
apps/jars/spark-terasort/target/spark-terasort-1.0.jar \
/terasort-output-10g
```

In the output you should see the following lines: 
```
num records: 1000000000
checksum: 1dcd3a102148ad22
partitions are properly sorted
```

The checksum should be the same what you get if you run the Terasort 
program on the input directory. 

## Contributions

PRs are always welcome. Please fork, and make necessary modifications 
you propose, and let us know. 

## Contact 

If you have questions or suggestions, feel free to post at:

https://groups.google.com/forum/#!forum/zrlio-users

or email: zrlio-users@googlegroups.com
