# Dabarqus Apps

## pull

Download a model from a Hugging Face repository.

Usage: pull [options]
Options:
  --model <name>              Model moniker to download. Required. '[HuggingFace User]/[Repository Name]/[File Name]'.
  --force                     Force download even if the model already exists.
  --help, -?                  Show this help message

## store

Ingest documents into a Dabarqus Memory Bank for later retrieval during a conversation.

Usage: store [options]
Options:
  --input-path <path>         Path to the input directory or file. Required.
  --chunk-size <size>         Chunk size. Default: [dynamic based on embedding context size].
  --chunk-overlap <percent>   Percentage of overlap between chunks. Default: 20.
  --memory-bank <name>        Output file base name. Default: embeddings.
  --help, -?                  Show this help message.

## retrieve

Retrieve documents from a Dabarqus Memory Bank for use in a conversation.

Usage: retrieve [options]
Options:
  --memory-bank <name>        Input file base name. Default: embeddings
  --query <query>             Query to run against the embeddings. Default: [ask user at runtime]
  --help, -?                  Show this help message
