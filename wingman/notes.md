# Wingman Server REST API Documentation

The Wingman server provides a REST API for interacting with large language models (LLMs) for various tasks, including text completion, chat, infilling, tokenization, and embeddings. Additionally, it offers a WebSocket interface for real-time communication and monitoring.

**Note**: This documentation reflects the state of the server code as of November 2023.

## Authentication

The server supports optional API key authentication for enhanced security. If API keys are configured, all requests to protected endpoints must include a valid key in the `Authorization` header with the `Bearer` scheme.

## Endpoints

### Health Check

- Endpoint: `/health`
- Method: `GET`
- Description: Checks the server's health and reports the number of idle and processing slots.
- Optional Parameters:
  - `include_slots`: Boolean. If true, includes detailed information about each slot in the response.
  - `fail_on_no_slot`: Boolean. If true, returns a 503 Service Unavailable status if no slots are available.
- Response: JSON object with the following fields:
  - status: String. Either "ok" or "no slot available".
  - slots_idle: Integer. Number of idle slots.
  - slots_processing: Integer. Number of processing slots.
  - slots: (Optional) Array of JSON objects containing detailed information about each slot (if include_slots is true).

### Slots Information

- Endpoint: /slots
- Method: GET
- Description: Retrieves detailed information about all slots, including their state, prompt, and generation settings.
- Response: Array of JSON objects containing information about each slot.

### Prometheus Metrics

- Endpoint: /metrics
- Method: GET
- Description: Provides Prometheus-compatible metrics about the server's performance and resource usage.
- Response: Plain text formatted according to the Prometheus exposition format.

### Server Properties

- Endpoint: /props
- Method: GET
- Description: Retrieves server properties, including user and assistant names, default generation settings, and the total number of slots.
- Response: JSON object with server properties.

### Model Listing

- Endpoint: /v1/models
- Method: GET
- Description: Lists available models and their metadata.
- Response: JSON object with a list of models.

### Text Completion

- Endpoint: /completions, /v1/completions
- Method: POST
- Description: Generates text completions based on a given prompt and parameters.
- Request Body: JSON object with the following fields:
  - prompt: String or array of strings. The prompt for text generation.
  - stream: (Optional) Boolean. If true, streams the generated text as chunks.
  - (See server code for additional optional parameters related to sampling, stopping criteria, etc.)
- Response:
  - Streaming Mode: Server-Sent Events (SSE) stream with JSON objects containing the generated text chunks.
  - Non-Streaming Mode: JSON object with the complete generated text and other information.

### Chat Completion

- Endpoints: /chat/completions, /v1/chat/completions
- Method: POST
- Description: Conducts a chat conversation with the LLM, similar to the ChatGPT API.
- Request Body: JSON object with the following fields:
  - messages: Array of JSON objects, each representing a message in the chat history.
  - model: (Optional) String. The ID of the model to use.
  - (See server code for additional optional parameters related to streaming, sampling, etc.)
- Response:
  - Streaming Mode: SSE stream with JSON objects containing the generated chat responses.
  - Non-Streaming Mode: JSON object with the complete chat conversation and other information.

### Text Infilling

- Endpoint: /infill
- Method: POST
- Description: Fills in missing parts of a text, given a prefix and suffix.
- Request Body: JSON object with the following fields:
  - prompt: String or array of strings. The text with missing parts.
  - input_prefix: String. The prefix before the missing part.
  - input_suffix: String. The suffix after the missing part.
  - (See server code for additional optional parameters)
- Response:
  - Streaming Mode: SSE stream with JSON objects containing the generated infill chunks.
  - Non-Streaming Mode: JSON object with the complete infilled text and other information.

### Tokenization

- Endpoint: /tokenize
- Method: POST
- Description: Tokenizes a given text into a sequence of tokens.
- Request Body: JSON object with the following field:
  - content: String. The text to tokenize.
- Response: JSON object with a list of token IDs.

### Detokenization

- Endpoint: /detokenize
- Method: POST
- Description: Converts a sequence of token IDs back into text.
- Request Body: JSON object with the following field:
  - tokens: Array of integers. The token IDs to detokenize.
- Response: JSON object with the detokenized text.

### Embeddings

- Endpoints: /embeddings, /v1/embeddings
- Method: POST
- Description: Generates embedding vectors for a given text or tokens.
- Request Body: JSON object with the following fields:
  - input: String or array of strings/integers. The text or tokens to embed.
- Response: JSON object with a list of embedding vectors.

### Slot Save/Restore/Erase

- Endpoint: /slots/:id_slot
- Method: POST
- Description: Manages the state of a specific slot by saving, restoring, or erasing its KV cache.
- Parameters:
  - id_slot: Integer. The ID of the slot to manage.
  - action: String. The action to perform (either "save", "restore", or "erase").
- Request Body: (For save/restore actions) JSON object with the following field:
  - filename: String. The filename to use for saving or restoring the slot's state.
- Response: JSON object with information about the performed action.

## WebSocket Interface

The server provides a WebSocket interface for real-time communication and monitoring. Clients can connect to the WebSocket server and receive various events and messages, including:

- Timing Metrics: Periodic updates containing timing information about prompt processing and token generation.
- Server Status Updates: Notifications about server state changes, such as model loading or errors.
- Custom Messages: Applications can implement custom message types for specific purposes.

## Additional Notes

- The server supports various configuration options through command-line arguments.
- Error responses are formatted as JSON objects with an `error` field containing details about the error.
- The server logs information about requests, responses, and internal events.
- Refer to the server code for further details about specific parameters, response formats, and implementation specifics.

# Wingman Server /api Endpoints and JSON Object Types

This documentation focuses on the `/api` endpoints offered by the Wingman server and the structure of JSON objects returned by these endpoints.

## `/api` Endpoints

The `/api` endpoints provide access to various functionalities related to model management, download management, inference control, and system information.

### Model Management

- Endpoint: /api/models
- Method: GET
- Description: Retrieves a list of available AI models with their details.
- Response: JSON object with the following structure:

```json
{
  "models": [
    {
      "isa": "AIModel",
      "id": "string",
      "name": "string",
      "maxLength": 0,
      "tokenLimit": 0,
      "vendor": "string",
      "location": "string",
      "items": [ /* Array of DownloadableItem objects */ ],
      "downloads": 0,
      "likes": 0,
      "created": "string",
      "updated": "string",
      "size": "string",
      "iQScore": 0,
      "eQScore": 0,
      "isInferable": false,
      "totalMemory": 0,
      "availableMemory": 0,
      "normalizedQuantizedMemRequired": 0
    },
    /*... more AIModel objects */
  ]
}
```

- Object Type: AIModel
  - isa: String. Always "AIModel".
  - id: String. Unique identifier of the model.
  - name: String. Name of the model.
  - maxLength: Integer. Maximum context length for the model.
  - tokenLimit: Integer. Token limit for the model.
  - vendor: String. Vendor or organization providing the model.
  - location: String. Location or source of the model.
  - items: Array of DownloadableItem objects representing different versions or quantizations of the model.
  - downloads: Integer. Number of downloads for the model.
  - likes: Integer. Number of likes for the model.
  - created: String. Date and time when the model was created.
  - updated: String. Date and time when the model was last updated.
  - size: String. Size of the model file.
  - iQScore: Double. IQ score of the model (a metric evaluating the model's performance).
  - eQScore: Double. EQ score of the model (a metric evaluating the model's emotional intelligence).
  - isInferable: Boolean. Whether the model can be used for inference.
  - totalMemory: Integer. Total system memory.
  - availableMemory: Integer. Available system memory.
  - normalizedQuantizedMemRequired: Integer. Normalized memory required for the quantized model.

- Object Type: DownloadableItem
  - isa: String. Always "DownloadableItem".
  - modelRepo: String. Model repository or source.
  - modelRepoName: String. Name of the model repository.
  - filePath: String. Path to the model file within the repository.
  - quantization: String. Quantization level of the model (e.g., "q4_0").
  - quantizationName: String. Readable name of the quantization level.
  - isDownloaded: Boolean. Whether the model file has been downloaded.
  - available: Boolean. Whether the model is available for download.
  - hasError: Boolean. Whether there was an error downloading the model.
  - location: String. Location or path of the downloaded model file.
  - isInferable: Boolean. Whether the model can be used for inference.
  - normalizedQuantizedMemRequired: Integer. Normalized memory required for the quantized model.

### Download Management

#### List Downloads

- Endpoint: /api/downloads
- Method: GET
- Description: Retrieves information about model download tasks.
- Optional Parameters:
  - modelRepo: String. Filters results to a specific model repository.
  - filePath: String. Filters results to a specific file path within a repository.
- Response: JSON object with the following structure:

```json
{
  "DownloadItems": [
    {
      "isa": "DownloadItem",
      "modelRepo": "string",
      "filePath": "string",
      "status": "idle", /* or other DownloadItemStatus values */
      "totalBytes": 0,
      "downloadedBytes": 0,
      "downloadSpeed": "string",
      "progress": 0,
      "metadata": "{}",
      "error": "string",
      "created": 0,
      "updated": 0
    },
    /* ... more DownloadItem objects */
  ]
}
```

- Object Type: DownloadItem
  - isa: String. Always "DownloadItem".
  - modelRepo: String. Model repository or source.
  - filePath: String. Path to the model file within the repository.
  - status: String. Current status of the download task (e.g., "idle", "queued", "downloading", "complete", "error", "cancelled").
  - totalBytes: Integer. Total size of the model file in bytes.
  - downloadedBytes: Integer. Number of bytes downloaded so far.
  - downloadSpeed: String. Current download speed.
  - progress: Double. Progress of the download as a value between 0.0 and 1.0.
  - metadata: String. JSON string containing metadata about the model.
  - error: String. Error message if the download failed.
  - created: Integer. Timestamp when the download task was created.
  - updated: Integer. Timestamp when the download task was last updated.

#### Downloading, Cancelling, and Resetting a Model Download

- Endpoint: /api/downloads/enqueue
- Method: GET
- Description: Enqueues a new model download task.
- Parameters:
  - modelRepo: String. The model repository or source.
  - filePath: String. Path to the model file within the repository.
- Response: JSON object representing the created DownloadItem.

- Endpoint: /api/downloads/cancel
- Method: GET
- Description: Cancels a model download task.
- Parameters:
  - modelRepo: String. The model repository or source.
  - filePath: String. Path to the model file within the repository.
- Response: JSON object representing the cancelled DownloadItem.

- Endpoint: /api/downloads/reset
- Method: GET
- Description: Deletes a model download task and its associated files.
- Parameters:
  - modelRepo: String. The model repository or source.
  - filePath: String. Path to the model file within the repository.
- Response: JSON object representing the deleted DownloadItem.

### Inference Control

#### List Inference Tasks

- Endpoint: /api/inference
- Method: GET
- Description: Retrieves information about inference tasks (running or queued LLMs).
- Optional Parameters:
  - alias: String. Filters results to a specific inference task identified by its alias.
- Response: JSON object with the following structure:

```json
{
  "WingmanItems": [
    {
      "isa": "WingmanItem",
      "alias": "string",
      "status": "queued", /* or other WingmanItemStatus values */
      "modelRepo": "string",
      "filePath": "string",
      "address": "string",
      "port": 0,
      "contextSize": 0,
      "gpuLayers": 0,
      "chatTemplate": "string",
      "force": false,
      "error": "string",
      "created": 0,
      "updated": 0
    },
    /* ... more WingmanItem objects */
  ]
}
```

- Object Type: WingmanItem
  - isa: String. Always "WingmanItem".
  - alias: String. Unique alias assigned to the inference task.
  - status: String. Current status of the inference task (e.g., "queued", "preparing", "inferring", "complete", "error", "cancelling").
  - modelRepo: String. Model repository or source.
  - filePath: String. Path to the model file within the repository.
  - address: String. IP address where the inference server is running.
  - port: Integer. Port number of the inference server.
  - contextSize: Integer. Context size used for the LLM.
  - gpuLayers: Integer. Number of layers to offload to the GPU.
  - chatTemplate: String. Chat template used for formatting chat interactions.
  - force: Boolean. Whether to force the inference task to start even if another task is running.
  - error: String. Error message if the inference failed.
  - created: Integer. Timestamp when the inference task was created.
  - updated: Integer. Timestamp when the inference task was last updated.

#### Starting, Stopping, and Resetting an Inference Task

- Endpoint: /api/inference/start
- Method: GET
- Description: Starts a new inference task with a specified model and parameters.
- Parameters:
  - alias: String. Unique alias to assign to the inference task.
  - modelRepo: String. Model repository or source.
  - filePath: String. Path to the model file within the repository.
  - address: (Optional) String. IP address where the inference server should run.
  - port: (Optional) Integer. Port number for the inference server.
  - contextSize: (Optional) Integer. Context size to use for the LLM.
  - gpuLayers: (Optional) Integer. Number of layers to offload to the GPU.
  - chatTemplate: (Optional) String. Chat template to use for formatting chat interactions.
  - Response: JSON object representing the created WingmanItem.

- Endpoint: /api/inference/stop
- Method: GET
- Description: Stops a running inference task.
- Parameters:
  - alias: String. Alias of the inference task to stop.
- Response: JSON object representing the stopped WingmanItem.

- Endpoint: /api/inference/reset
- Method: GET
- Description: Stops and removes an inference task.
- Parameters:
  - alias: String. Alias of the inference task to reset.
- Response: JSON object representing the removed WingmanItem.

- Endpoint: /api/inference/status
- Method: GET
- Description: Retrieves the status of inference tasks.
- Optional Parameters:
  - alias: String. Filters results to a specific inference task identified by its alias.
- Response: JSON array of WingmanItem objects.

### System Information

- Endpoint: /api/hardware, /api/hardwareinfo
- Method: GET
- Description: Retrieves information about the system's hardware, including CPU, GPU, and memory.
- Response: JSON object containing details about the system's hardware.

- Endpoint: /api/shutdown
- Method: GET
- Description: Initiates a graceful shutdown of the Wingman server.
- Response: "200 OK" status with a message indicating shutdown.


### Logging

- Endpoint: /api/utils/log
- Method: POST
- Description: Writes a log message to the server's log file.
- Request Body: JSON object with the following structure:

```json
{
  "isa": "WingmanLogItem",
  "level": "info", /* or other WingmanLogLevel values*/
  "message": "string",
  "timestamp": 0
}
```

- Object Type: WingmanLogItem
  - isa: String. Always "WingmanLogItem".
  - level: String. Log level (e.g., "info", "warn", "error", "debug").
  - message: String. The log message.
  - timestamp: Integer. Timestamp of the log message.

### Additional Notes

The /api endpoints provide a convenient way to interact with the Wingman server programmatically.

Applications can use these endpoints to manage models, control inference tasks, and monitor the server's state and performance.
The JSON object types described above provide a structured way to exchange information between the server and clients.

# Wingman Server WebSocket API Documentation

The Wingman server utilizes a WebSocket API for real-time communication and data exchange between the server and connected clients. This allows for dynamic updates and monitoring without the need for continuous polling.

## Connection

Clients can establish a WebSocket connection to the server at the specified websocket port (default 6568). Once connected, they can receive various events and messages in real-time.

## Events and Messages

The server sends the following types of events and messages over the WebSocket connection:

1. Timing Metrics

- Event Type: metrics
- Data Format: JSON object containing timing information about prompt processing and token generation. The structure is as follows:

```json
{
  "alias": "string", /*Model alias */
  "meta": {}, /* Model metadata */
  "system": {}, /* System information */
  "tensors": {}, /* Tensor type information */
  "timings": {
    "timestamp": 0, /* Timestamp */
    "load_time": 0, /* Model loading time */
    "sample_time": 0, /* Total sampling time */
    "sample_count": 0, /* Number of samples */
    "sample_per_token_ms": 0, /* Average time per token */
    "sample_per_second": 0, /* Tokens per second */
    "total_time": 0, /* Total running time */
    /* ... other timing details*/
  }
}
```

- Description: Provides insights into the performance of the inference engine for a specific model.

2. Server Status Updates

- Event Type: status
- Data Format: JSON object containing information about the server's state.
- Description: Notifies clients about changes in the server's status, such as model loading, errors, or shutdown events.

3. Download Status Updates

- Event Type: download_status
- Data Format: JSON object representing a DownloadItem.
- Description: Provides updates on the progress and status of model download tasks.

4. Inference Status Updates

- Event Type: inference_status
- Data Format: JSON object representing a WingmanItem.
- Description: Provides updates on the progress and status of inference tasks.

5. Custom Messages

- Event Type: Application-specific
- Data Format: Application-specific
- Description: Applications can define and send custom messages over the WebSocket connection for specific purposes.

### Client Actions

Clients can also send messages to the server over the WebSocket connection:

- Message: shutdown
- Description: Requests a graceful shutdown of the Wingman server.

### Implementation Notes

The WebSocket server uses the uWebSockets library for efficient and scalable communication.

The server automatically manages WebSocket connections and handles disconnections gracefully.

Applications can leverage the WebSocket API to build interactive user interfaces, dashboards, or other real-time monitoring tools.
