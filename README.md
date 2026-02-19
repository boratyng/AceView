# Magic2

## Getting started

### Create index
```
sortalign --createIndex IDX -t target.fasta
```

### Map reads
```
sortalign -x IDX -i forward.fastq.gz+reverse.fastq.gz -o results
# SAM output
sortalign -x IDX -i forward.fastq.gz+reverse.fastq.gz -o results --sam
# Multiple runs
sortalign -x IDX -i forward.fastq.gz+reverse.fastq.gz,f1.fastq.gz+f2.fastq.gz -o results
# Report target coverage
sortalign -x IDX -i forward.fastq.gz+reverse.fastq.gz,f1.fastq.gz+f2.fastq.gz -o results --wiggle
# If you do not need alignments
sortalign -x IDX -i forward.fastq.gz+reverse.fastq.gz,f1.fastq.gz+f2.fastq.gz -o results --wiggle --noalign
```

## User's guide

Sortalign maps deep-sequencing runs and reports alignments as well as coverage plots and introns. In the first step, sortalign analyses the target sequences (specified by -t or --target) and creates an index. This is needed only once per target. Sortalign downloads data from NCBI Sequence Read Archive. Input can be a FASTA or FASTQ file or an NCBI SRA accession.

When mapping to a human genome, depending on hardware, the program uses 10 to 20 threads, requires about 30GB of memory, and processes about 1 Gigabase of human RNA-seq reads per minute.

### Index generation

Below are additional options for generating a target index with the default values given on the command line.
```
sortalign --createIndex IDX -t target.fasta --seedLength 18 --maxTargetRepeats 81
```

### Mapping sequence files
`sortalign` takes input files as a comma-separated list of file names or an NCBI SRA accession. File names for paired reads are separated by `+`. Use `-` for standard input. Files with names ending with ".gz" are automatically decompressed. Adding `--gzi` forces decompression of input files. Sortalign guesses file format from a file name, but one can also specify the format of input files with flags: `--fasta`, `--fastq`, `--fastc`, `--raw`, `-sra`.

```
sortalign -x IDX -i f1.fastq.gz+f2.fastq.gz -o results --sam
```

### NCBI SRA
`sortalign` can also download and map reads from an NCBI Sequence Read Archive (SRA). Just specify SRA accession in `-i` option.
```
sortalign -x IDX -i SRR24511885 -o results --sam
```

One can cache SRA reads on the local disk with `--sraCaching` option.


### Output

The `-o` option specifies a path to the output directory. By default `sortalign` reports alignments in its internal format. To request the SAM report, use `--sam` option. `--gzo` will gzip output files.

You can also generate target coverage wiggles in UCSC format with `--wiggles`.
```
sortalign -x IDX -i SRR24511885 -o results --wiggle
```
