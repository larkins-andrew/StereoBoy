import sys
from google import genai
import os
import re
from datetime import datetime



def connect():
# 1. The client automatically picks up the API key from the 
#    GEMINI_API_KEY environment variable.
    try:
        with open("env_variables","r") as file:
            API_KEY=file.readline().strip()
        client = genai.Client(api_key=API_KEY)
    except Exception:
        # Fallback: If environment variable isn't set, 
        # you can pass the key directly (less secure):
        # client = genai.Client(api_key="YOUR_API_KEY_HERE")
        print("API Key is not set in environment variable GEMINI_API_KEY. Please check setup.")
        exit()

    MODEL_NAME = "gemini-2.5-flash"
    print(MODEL_NAME,client)
    return (MODEL_NAME, client)

def generate_report(commit_message):
    MODEL_NAME,client = connect()
    with open("difference.txt", "r") as file:
        diffs = file.read()
    changed_files = get_diff_files(diffs)
    user_prompt = "Try to determine the thought process of the person based on the differences:"
    user_prompt+=diffs
    user_prompt+="as well as the files that were changed:"
    for filename in changed_files:
        user_prompt+=filename+":"
        with open(filename, "r") as file:
            user_prompt+=file.read()
    user_prompt+="and the commit message that was used:"
    user_prompt+=commit_message
    user_prompt+="Do not acknowledge the input, only write a report, explaining what was tried, and what was the goal of whoever changed the code. Keep your answer short. Write the answer in the first person, that is, use the word I a lot. The goal is to indicate what I have done and what I was thinking when doing so."
    user_prompt+="The rubric is the following:"
    user_prompt+="""
    Level of Detail Described: First, did you provide enough detail in each entry for others (such as a typical ECE senior level student) to fully understand exactly what project work you performed?
    Does each journal entry answer the six questions: What did you work on? How does this work build upon your team’s previous related work? How did you do that work (tools, programs, test rigs, etc.)? What was the result and what did you learn from this work? How does your work relate to your team's project progress? What are the next steps? Do NOT include irrelevant information, puffery language, or other “fluff.”
    """
    response = client.models.generate_content(
        model=MODEL_NAME,
        contents=user_prompt
    )
    now=datetime.now()
    filename = "reports/report_"+str(now.strftime("%Y-%m-%d_%H-%M-%S"))+".txt"
    with open(filename, "w") as file:
        file.write(response.text)


def get_diff_files(diffs):
    FILE_REGEX = r"^\s*diff --git a\/([^\s]+)"
    matches = re.findall(FILE_REGEX, diffs, re.MULTILINE)
    return(matches)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        message = sys.argv[1]
        generate_report(message)
