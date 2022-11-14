## Life of a pull request
1. Find the right problem to tackle, create a new one if not exists. Use milestone to organize issues for large projects.
2. (for the first PR of a operator, IO util etc) Create the dir in right place, and add a README.md which is a design doc of the component. The design doc should include the goal of the project, link to the issues, code structure, milestones etc.
3. (for code changes) Create .h, .cc, test file, .py file, as a standalone component of the project.
4. Push to remote branch, wait for the CI to pass, create a PR, and ask for a review in the dingtalk channel.
5. Address the comments from reviewers, and / or add more explanations, until all issues are resolved.
6. Merge (or ask the OWNERs to merge) the pull request.
7. Close the issue.
