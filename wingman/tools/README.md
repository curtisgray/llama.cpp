# Tools

  - tool.ingest: Ingest data from a file or URL into an embedding database that's stored in the user's Wingman directory.
  - tool.retrieve: Query an embedding database that's stored in the user's Wingman directory.

## Running Ingestion Tools

1. Start the Wingman server.

```bash
$ cd wingman.bin.directory
$ ./wingman
```

2. Start inference of a model.

  From Wingman, download and start an AI model. For example, download the `bert-base-uncased` model.

3. Run the ingestion tool.

```bash
$ tool.ingest --input-path /path/to/pdf-files
```

## Running Retrieval Tools

1. Start the Wingman server.

```bash
$ cd wingman.bin.directory
$ ./wingman
```

2. Start inference of a model.

  From Wingman, download and start an AI model. For example, download the `bert-base-uncased` model.

3. Run the retrieval tool.

```bash
$ tool.retrieve --query "query string"

# or
$ tool.retrieve
```
